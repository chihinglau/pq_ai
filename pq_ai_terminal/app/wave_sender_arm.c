/**
 * @file wave_sender_arm.c
 * @brief T536 波形采集发送端 - V2 协议版本
 * 
 * 协议版本: V2 (魔数 0x57415632 "WV2")
 * 
 * 帧格式:
 *   头部 (14字节): magic(4) + version(1) + cmd(1) + seq(4) + payload_len(4)
 *   CRC32 (4字节): 头部+数据的校验值
 *   Payload (N字节): 波形数据
 * 
 * AI 响应 (35字节):
 *   magic(4) + resp_type(1) + timestamp(8) + cycle_seq(4) + 
 *   if_score(4) + ae_score(4) + cnn_confidence(4) + cnn_class(1) + 
 *   scene_id(1) + crc32(4)
 * 
 * 用法: wave_sender_arm [options]
 *   --cycles N       采集周期数 (默认5)
 *   --interval MS    采集间隔毫秒 (默认500)
 *   --server IP      RK3576服务器IP (默认192.168.100.1)
 *   --port PORT      RK3576服务端口 (默认9090)
 *   --log FILE       日志文件名 (default: wave_sender.log)
 *
 * 运行: /lib32/ld-linux-armhf.so.3 --library-path /lib32:/custom/sys/lib/hal_lib/lib32 ./wave_sender_arm --cycles 5 --server 192.168.100.1
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ========== 类型定义 ========== */
/* 使用 <stdint.h> 标准类型确保跨平台 (ARM32/ARM64) 一致性 */
/* int32_t, uint32_t, uint64_t 已在 <stdint.h> 中定义 */
/* 这里定义别名以兼容原有代码 */
typedef int32_t int32;
typedef uint32_t uint32;

/* ========== HAL函数声明 ========== */
struct tag_HW_DEVICE;
int32 hal_init(void);
int32 hal_exit(void);
struct tag_HW_DEVICE *hal_device_get(const char *device_id);
int32 hal_device_release(struct tag_HW_DEVICE *dev);

/* ========== 设备定义 ========== */
#define WAVEFORM_SAMPLER_HARDWARE_MODULE_ID "waveform_sampler"

struct tag_HW_MODULE;
typedef struct tag_HW_DEVICE {
    struct tag_HW_MODULE *pModule;
    int32 nVer;
    const char *szDeviceID;
} HW_DEVICE;

typedef struct tag_WAVEFORM_SAMPLER_DEVICE {
    HW_DEVICE base;
    int32 (*read_waveform)(struct tag_WAVEFORM_SAMPLER_DEVICE *dev,
                           void *data, int32 dsize, uint32 lastn);
    int32 (*read_real_monitor_data)(struct tag_WAVEFORM_SAMPLER_DEVICE *dev,
                                    void *data, int32 dsize, uint32 lastn);
} WAVEFORM_SAMPLER_DEVICE_T;

/* ========== 常量 ========== */
#define WS_SINGLE_WAVEFORM_SIZE 7182
#define WS_POINTS_PER_CYCLE 256
#define WS_CHANNEL_COUNT 7
#define WAVE_BUF_SIZE (WS_SINGLE_WAVEFORM_SIZE * 20)

/* V2 协议常量 */
#define PROTO_MAGIC 0x57415632  /* "WV2" - Wave Protocol V2 */
#define PROTO_VERSION 2

/* 命令类型 */
#define CMD_WAVEFORM 0x01
#define CMD_ACK 0x03

/* 响应类型 */
#define RESP_OK 0
#define RESP_ANOMALY 2

/* 帧头大小 (不包括CRC32) */
#define FRAME_HEADER_SIZE 14

/* AI 响应负载大小 (V2帧内的payload) */
#define AI_RESPONSE_PAYLOAD_SIZE 35

/* AI 响应命令类型 (V2协议) */
#define CMD_AI_RESULT 0x07

/* USB ECM 配置 */
#define DEFAULT_SERVER_IP "192.168.100.1"
#define DEFAULT_SERVER_PORT 9090
#define DEFAULT_TIMEOUT_MS 5000

/* ========== CRC32 实现 (与 zlib.crc32 兼容, 使用多项式 0xEDB88320) ========== */
static uint32_t crc32_table[256];
static int crc32_table_initialized = 0;

static void crc32_init_table(void)
{
    uint32_t i, j, c;
    for (i = 0; i < 256; i++) {
        c = i;
        for (j = 0; j < 8; j++) {
            if (c & 1)
                c = (c >> 1) ^ 0xEDB88320;
            else
                c = c >> 1;
        }
        crc32_table[i] = c;
    }
    crc32_table_initialized = 1;
}

static uint32_t crc32_calc(const uint8_t *data, int len)
{
    uint32_t crc = 0xFFFFFFFF;
    int i;
    if (!crc32_table_initialized) {
        crc32_init_table();
    }
    for (i = 0; i < len; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    }
    return crc ^ 0xFFFFFFFF;
}

/* ========== 协议结构 (V2) ========== */
/* 传输头 - 14字节 (使用packed防止对齐填充) */
typedef struct __attribute__((packed)) {
    uint32_t magic;         /* 魔数: 0x57415632 ("WV2") (4) */
    uint8_t  version;       /* 协议版本 (1) */
    uint8_t  cmd;           /* 命令类型 (1) */
    uint32_t seq;           /* 序列号 (4) - 用于可靠传输 */
    uint32_t payload_len;   /* 负载长度 (4) */
} proto_header_v2_t;
/* 总计: 4+1+1+4+4 = 14字节 */

/* AI推理响应 - 35字节 (使用packed防止对齐填充) */
typedef struct __attribute__((packed)) {
    uint32_t magic;         /* 魔数: 0x57415645 (4) */
    uint8_t  resp_type;     /* 响应类型: 0=OK, 2=ANOMALY (1) */
    uint64_t timestamp;     /* 时间戳微秒 (8) */
    uint32_t cycle_seq;     /* 周波序号 (4) */
    float    if_score;      /* iForest异常得分 (4) */
    float    ae_score;      /* AE重构误差 (4) */
    float    cnn_confidence;/* CNN置信度 (4) */
    uint8_t  cnn_class;     /* CNN事件分类 (1) */
    uint8_t  scene_id;      /* 场景ID (1) */
    uint32_t crc32;         /* CRC32校验 (4) */
} ai_response_v2_t;
/* 总计: 4+1+8+4+4+4+4+1+1+4 = 35字节 */

/* ========== 日志系统 ========== */
static FILE *g_log_fp = NULL;
static int g_log_level = 2;  /* 0=ERROR, 1=WARN, 2=INFO, 3=DEBUG */

#define LOG_ERROR(fmt, ...) do { \
    time_t now = time(NULL); \
    char time_str[64]; \
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now)); \
    printf("[%s] [ERROR] " fmt "\n", time_str, ##__VA_ARGS__); \
    if (g_log_fp) { \
        fprintf(g_log_fp, "[%s] [ERROR] " fmt "\n", time_str, ##__VA_ARGS__); \
        fflush(g_log_fp); \
    } \
} while(0)

#define LOG_WARN(fmt, ...) do { \
    if (g_log_level >= 1) { \
        time_t now = time(NULL); \
        char time_str[64]; \
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now)); \
        printf("[%s] [WARN] " fmt "\n", time_str, ##__VA_ARGS__); \
        if (g_log_fp) { \
            fprintf(g_log_fp, "[%s] [WARN] " fmt "\n", time_str, ##__VA_ARGS__); \
            fflush(g_log_fp); \
        } \
    } \
} while(0)

#define LOG_INFO(fmt, ...) do { \
    if (g_log_level >= 2) { \
        time_t now = time(NULL); \
        char time_str[64]; \
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now)); \
        printf("[%s] [INFO] " fmt "\n", time_str, ##__VA_ARGS__); \
        if (g_log_fp) { \
            fprintf(g_log_fp, "[%s] [INFO] " fmt "\n", time_str, ##__VA_ARGS__); \
            fflush(g_log_fp); \
        } \
    } \
} while(0)

#define LOG_DEBUG(fmt, ...) do { \
    if (g_log_level >= 3) { \
        time_t now = time(NULL); \
        char time_str[64]; \
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now)); \
        printf("[%s] [DEBUG] " fmt "\n", time_str, ##__VA_ARGS__); \
        if (g_log_fp) { \
            fprintf(g_log_fp, "[%s] [DEBUG] " fmt "\n", time_str, ##__VA_ARGS__); \
            fflush(g_log_fp); \
        } \
    } \
} while(0)

/* ========== 辅助函数 ========== */
static uint32_t read_le32(const uint8_t *p) {
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t read_le64(const uint8_t *p) {
    return ((uint64_t)p[0]) |
           ((uint64_t)p[1] << 8) |
           ((uint64_t)p[2] << 16) |
           ((uint64_t)p[3] << 24) |
           ((uint64_t)p[4] << 32) |
           ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) |
           ((uint64_t)p[7] << 56);
}

/* ========== 函数声明 ========== */
static int connect_to_server(const char *ip, int port, int timeout_ms);
static int send_waveform_v2(int sock_fd, uint8_t *wave_data, int wave_len, 
                            uint32_t seq);
static int receive_ai_result_v2(int sock_fd, ai_response_v2_t *result, int timeout_ms);
static void print_wave_summary(uint8_t *wave_data, int wave_len);

/* ========== 连接服务器 ========== */
static int connect_to_server(const char *ip, int port, int timeout_ms)
{
    int sock_fd;
    struct sockaddr_in serv_addr;
    struct timeval tv;
    
    LOG_INFO("Connecting to server %s:%d (timeout: %dms)...", ip, port, timeout_ms);
    
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        LOG_ERROR("Socket creation failed (errno=%d: %s)", errno, strerror(errno));
        return -1;
    }
    
    /* 设置超时 */
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &serv_addr.sin_addr);
    
    if (connect(sock_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        LOG_ERROR("Connection failed to %s:%d (errno=%d: %s)", ip, port, errno, strerror(errno));
        close(sock_fd);
        return -1;
    }
    
    LOG_INFO("Connected successfully to %s:%d", ip, port);
    return sock_fd;
}

/* ========== 发送波形数据 (V2协议) ========== */
static int send_waveform_v2(int sock_fd, uint8_t *wave_data, int wave_len,
                            uint32_t seq)
{
    proto_header_v2_t header;
    uint8_t send_buf[WAVE_BUF_SIZE + FRAME_HEADER_SIZE + 4];
    int total_len;
    int sent;
    uint32_t crc;
    
    /* 构建 V2 协议头 */
    header.magic = PROTO_MAGIC;
    header.version = PROTO_VERSION;
    header.cmd = CMD_WAVEFORM;
    header.seq = seq;
    header.payload_len = wave_len;
    
    /* 组合头和数据 */
    memcpy(send_buf, &header, sizeof(header));
    memcpy(send_buf + sizeof(header), wave_data, wave_len);
    
    /* 计算 CRC32 (头部+数据) */
    crc = crc32_calc(send_buf, sizeof(header) + wave_len);
    memcpy(send_buf + sizeof(header) + wave_len, &crc, 4);
    
    total_len = sizeof(header) + wave_len + 4;  /* header + payload + crc */
    
    LOG_INFO("Sending waveform V2: seq=%u, payload_len=%d, total=%d bytes", 
             seq, wave_len, total_len);
    LOG_DEBUG("Header: magic=0x%08X, ver=%d, cmd=%d, seq=%u, payload_len=%u",
             header.magic, header.version, header.cmd, header.seq, header.payload_len);
    LOG_DEBUG("CRC32: 0x%08X", crc);
    
    /* 验证魔数 */
    if (header.magic != PROTO_MAGIC) {
        LOG_ERROR("Invalid magic in header: 0x%08X (expected 0x%08X)", header.magic, PROTO_MAGIC);
        return -1;
    }
    
    /* 发送 */
    sent = send(sock_fd, send_buf, total_len, 0);
    if (sent <= 0) {
        LOG_ERROR("Send failed: sent=%d (errno=%d: %s)", sent, errno, strerror(errno));
        return -1;
    }
    
    LOG_INFO("Sent %d bytes successfully", sent);
    
    /* 验证发送完整性 */
    if (sent < total_len) {
        LOG_WARN("Incomplete send: %d/%d bytes", sent, total_len);
    }
    
    return 0;
}

/* ========== 接收AI推理结果 (V2协议帧) ========== */
static int receive_ai_result_v2(int sock_fd, ai_response_v2_t *result, int timeout_ms)
{
    struct timeval tv;
    fd_set readfds;
    int ret;
    int n;
    uint8_t frame_buf[FRAME_HEADER_SIZE + 4 + AI_RESPONSE_PAYLOAD_SIZE];
    uint8_t *header_ptr;
    uint8_t *crc_ptr;
    uint8_t *payload_ptr;
    uint32_t received_crc, calc_crc;
    uint32_t magic;
    uint8_t version, cmd;
    uint32_t seq, payload_len;
    
    /* 设置超时 */
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    
    LOG_INFO("Waiting for AI response (V2 frame, timeout: %dms)...", timeout_ms);
    
    /* 等待数据 */
    FD_ZERO(&readfds);
    FD_SET(sock_fd, &readfds);
    
    ret = select(sock_fd + 1, &readfds, NULL, NULL, &tv);
    if (ret <= 0) {
        if (ret == 0) {
            LOG_ERROR("Timeout waiting for AI response (select returned 0)");
        } else {
            LOG_ERROR("select() failed (errno=%d: %s)", errno, strerror(errno));
        }
        return -1;
    }
    
    /* 步骤1: 读取 V2 帧头 (14字节) */
    header_ptr = frame_buf;
    n = recv(sock_fd, header_ptr, FRAME_HEADER_SIZE, 0);
    if (n < FRAME_HEADER_SIZE) {
        LOG_ERROR("Incomplete V2 header: got %d bytes, expected %d", n, FRAME_HEADER_SIZE);
        return -1;
    }
    
    /* 解析 V2 帧头 */
    magic = read_le32(header_ptr);
    version = header_ptr[4];
    cmd = header_ptr[5];
    seq = read_le32(header_ptr + 6);
    payload_len = read_le32(header_ptr + 10);
    
    LOG_DEBUG("V2 frame header: magic=0x%08X, ver=%d, cmd=%d, seq=%u, payload_len=%u",
              magic, version, cmd, seq, payload_len);
    
    /* 验证 V2 协议魔数 */
    if (magic != PROTO_MAGIC) {
        LOG_ERROR("Invalid V2 magic: 0x%08X (expected 0x%08X)", magic, PROTO_MAGIC);
        return -1;
    }
    
    /* 验证命令类型 */
    if (cmd != CMD_AI_RESULT) {
        LOG_ERROR("Not an AI result frame: cmd=%d (expected %d)", cmd, CMD_AI_RESULT);
        return -1;
    }
    
    /* 验证负载长度 */
    if (payload_len != AI_RESPONSE_PAYLOAD_SIZE) {
        LOG_ERROR("Unexpected payload length: %u (expected %d)", payload_len, AI_RESPONSE_PAYLOAD_SIZE);
        return -1;
    }
    
    /* 步骤2: 读取 CRC32 (4字节) */
    crc_ptr = frame_buf + FRAME_HEADER_SIZE;
    n = recv(sock_fd, crc_ptr, 4, 0);
    if (n < 4) {
        LOG_ERROR("Incomplete CRC32: got %d bytes, expected 4", n);
        return -1;
    }
    
    /* 步骤3: 读取负载 (AI响应数据, 35字节) */
    payload_ptr = frame_buf + FRAME_HEADER_SIZE + 4;
    n = recv(sock_fd, payload_ptr, payload_len, 0);
    if (n < (int)payload_len) {
        LOG_ERROR("Incomplete AI payload: got %d bytes, expected %u", n, payload_len);
        return -1;
    }
    
    LOG_DEBUG("Received V2 frame: header=%d+crc=4+payload=%u bytes", FRAME_HEADER_SIZE, payload_len);
    
    /* 步骤4: 验证 V2 帧 CRC32 (头部+负载) */
    received_crc = read_le32(crc_ptr);
    calc_crc = crc32_calc(frame_buf, FRAME_HEADER_SIZE + payload_len);
    if (received_crc != calc_crc) {
        LOG_ERROR("V2 frame CRC32 mismatch: received=0x%08X, calculated=0x%08X", received_crc, calc_crc);
        return -1;
    }
    LOG_DEBUG("V2 frame CRC32 verified: 0x%08X", received_crc);
    
    /* 步骤5: 从负载解析 AI 响应结构 (逐字段解析, 确保字节序兼容性) */
    {
        uint8_t *p = payload_ptr;
        result->magic = read_le32(p); p += 4;
        result->resp_type = *p++;
        result->timestamp = read_le64(p); p += 8;
        result->cycle_seq = read_le32(p); p += 4;
        memcpy(&result->if_score, p, 4); p += 4;
        memcpy(&result->ae_score, p, 4); p += 4;
        memcpy(&result->cnn_confidence, p, 4); p += 4;
        result->cnn_class = *p++;
        result->scene_id = *p++;
        result->crc32 = read_le32(p); p += 4;
    }
    
    /* 验证 AI 响应魔数 (负载内部) */
    if (result->magic != 0x57415645) {
        LOG_ERROR("Invalid AI response magic: 0x%08X", result->magic);
        return -1;
    }
    
    /* 验证 AI 响应 CRC32 (基于负载前 31 字节, 不包括 crc32 字段) */
    received_crc = result->crc32;
    calc_crc = crc32_calc(payload_ptr, AI_RESPONSE_PAYLOAD_SIZE - 4);
    if (received_crc != calc_crc) {
        LOG_ERROR("AI response CRC32 mismatch: received=0x%08X, calculated=0x%08X", received_crc, calc_crc);
        return -1;
    }
    
    /* 打印响应内容 */
    LOG_INFO("AI Response received (V2 frame, seq=%u):", seq);
    LOG_INFO("  Magic: 0x%08X", result->magic);
    LOG_INFO("  Response Type: %s (resp_type=%d)", 
            result->resp_type == RESP_OK ? "OK" : "ANOMALY", result->resp_type);
    LOG_INFO("  Timestamp: %llu us", (unsigned long long)result->timestamp);
    LOG_INFO("  Cycle Seq: %u", result->cycle_seq);
    LOG_INFO("  iForest Score: %.4f", result->if_score);
    LOG_INFO("  AE Score: %.4f", result->ae_score);
    LOG_INFO("  CNN Class: %d", result->cnn_class);
    LOG_INFO("  CNN Confidence: %.2f", result->cnn_confidence);
    LOG_INFO("  Scene ID: %d", result->scene_id);
    LOG_INFO("  CRC32: 0x%08X", result->crc32);
    
    return 0;
}

/* ========== 打印波形摘要 ========== */
static void print_wave_summary(uint8_t *wave_data, int wave_len)
{
    /* 查找帧头 */
    int header_found = 0;
    for (int i = 0; i < wave_len - 1; i++) {
        if (wave_data[i] == 0x68 && wave_data[i + 1] == 0x36) {
            header_found = 1;
            uint32_t frame_len = read_le32(&wave_data[i + 2]);
            uint8_t frame_seq_byte = wave_data[i + 6];
            uint32_t cycle_seq = read_le32(&wave_data[i + 7]);
            
            LOG_INFO("Wave frame found at offset %d:", i);
            LOG_INFO("  Frame length: %u bytes", frame_len);
            LOG_INFO("  Frame seq: %u", frame_seq_byte);
            LOG_INFO("  Cycle seq: %u", cycle_seq);
            
            /* 解析时间戳 (在cycle_seq后4字节) */
            if (i + 7 + 4 + 10 <= wave_len) {
                uint8_t year = wave_data[i + 7 + 4];
                uint8_t month = wave_data[i + 7 + 5];
                uint8_t day = wave_data[i + 7 + 6];
                uint8_t hour = wave_data[i + 7 + 7];
                uint8_t minute = wave_data[i + 7 + 8];
                uint8_t second = wave_data[i + 7 + 9];
                uint32_t us = read_le32(&wave_data[i + 7 + 10]);
                LOG_INFO("  Timestamp: %04d-%02d-%02d %02d:%02d:%02d.%04u",
                        year + 2000, month, day, hour, minute, second, us / 1000);
            }
            
            /* 读取前几个通道点做快速验证 */
            int data_offset = i + 7 + 4 + 10;  /* 帧头(2)+帧长(4)+帧序号(1)+cycle_seq(4)+时间戳(10) */
            if (data_offset + 16 <= wave_len) {
                float ua_val, ub_val, uc_val;
                uint32_t raw;
                
                memcpy(&raw, &wave_data[data_offset], 4);
                memcpy(&ua_val, &raw, 4);
                LOG_INFO("  UA[0]: %.3f V (raw=0x%08X)", ua_val, raw);
                
                memcpy(&raw, &wave_data[data_offset + 256 * 4], 4);
                memcpy(&ub_val, &raw, 4);
                LOG_INFO("  UB[0]: %.3f V (raw=0x%08X)", ub_val, raw);
                
                memcpy(&raw, &wave_data[data_offset + 256 * 4 * 2], 4);
                memcpy(&uc_val, &raw, 4);
                LOG_INFO("  UC[0]: %.3f V (raw=0x%08X)", uc_val, raw);
            }
            break;
        }
    }
    
    if (!header_found) {
        LOG_WARN("No valid frame header found in waveform data!");
    }
}

/* ========== 主函数 ========== */
int main(int argc, char *argv[])
{
    int max_cycles = 5;
    int interval_ms = 500;
    const char *server_ip = DEFAULT_SERVER_IP;
    int server_port = DEFAULT_SERVER_PORT;
    const char *log_file = "wave_sender_v2.log";
    
    int i, ret;
    WAVEFORM_SAMPLER_DEVICE_T *wave_dev = NULL;
    HW_DEVICE *dev = NULL;
    uint8_t *wave_buf = NULL;
    uint32_t send_seq = 0;  /* V2 协议序列号 */

    /* 初始化 CRC32 表 */
    crc32_init_table();

    /* 解析命令行参数 */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--cycles") == 0 && i + 1 < argc) {
            max_cycles = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
            interval_ms = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--server") == 0 && i + 1 < argc) {
            server_ip = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            server_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            log_file = argv[++i];
        } else if (strcmp(argv[i], "--debug") == 0) {
            g_log_level = 3;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: wave_sender_arm [options]\n");
            printf("Options:\n");
            printf("  --cycles N       Number of cycles (default: 5)\n");
            printf("  --interval MS    Interval ms (default: 500)\n");
            printf("  --server IP      RK3576 server IP (default: 192.168.100.1)\n");
            printf("  --port PORT      RK3576 port (default: 9090)\n");
            printf("  --log FILE       Log file (default: wave_sender_v2.log)\n");
            printf("  --debug          Enable debug level logging\n");
            return 0;
        }
    }

    /* 打开日志文件 */
    g_log_fp = fopen(log_file, "w");
    if (g_log_fp) {
        setvbuf(g_log_fp, NULL, _IOLBF, 0);  /* 行缓冲 */
    }
    
    LOG_INFO("============================================================");
    LOG_INFO("T536 Wave Collector & AI Sender (V2 Protocol)");
    LOG_INFO("Protocol: WV2 (magic=0x%08X)", PROTO_MAGIC);
    LOG_INFO("Flow: Collect -> Send to RK3576 -> Receive AI Result");
    LOG_INFO("============================================================");
    LOG_INFO("Configuration:");
    LOG_INFO("  Cycles: %d", max_cycles);
    LOG_INFO("  Interval: %dms", interval_ms);
    LOG_INFO("  Server: %s:%d", server_ip, server_port);
    LOG_INFO("  Log file: %s", log_file);
    LOG_INFO("  Log level: %s", g_log_level >= 3 ? "DEBUG" : g_log_level >= 2 ? "INFO" : g_log_level >= 1 ? "WARN" : "ERROR");
    LOG_INFO("");

    /* ========== 初始化HAL ========== */
    LOG_INFO("Initializing HAL...");
    ret = hal_init();
    if (ret != 0) {
        LOG_ERROR("HAL initialization failed: %ld", ret);
        hal_exit();
        if (g_log_fp) fclose(g_log_fp);
        return 1;
    }
    LOG_INFO("HAL initialized successfully");

    /* ========== 获取设备 ========== */
    LOG_INFO("Getting waveform sampler device (id='%s')...", WAVEFORM_SAMPLER_HARDWARE_MODULE_ID);
    dev = hal_device_get(WAVEFORM_SAMPLER_HARDWARE_MODULE_ID);
    if (dev == NULL) {
        LOG_ERROR("Failed to get waveform sampler device!");
        LOG_ERROR("Troubleshooting:");
        LOG_ERROR("  1. Ensure device is connected and powered on");
        LOG_ERROR("  2. Check /custom/sys/lib/hal_lib/lib32/ for required libraries");
        LOG_ERROR("  3. Try: LD_PRELOAD=/custom/sys/lib/hal_lib/lib32/libdrivers.so");
        hal_exit();
        if (g_log_fp) fclose(g_log_fp);
        return 1;
    }
    wave_dev = (WAVEFORM_SAMPLER_DEVICE_T *)dev;
    LOG_INFO("Device obtained successfully!");
    LOG_INFO("  pModule: %p", (void*)wave_dev->base.pModule);
    LOG_INFO("  nVer: %ld", wave_dev->base.nVer);
    
    if (wave_dev->read_waveform == NULL) {
        LOG_ERROR("read_waveform function pointer is NULL!");
        hal_device_release(dev);
        hal_exit();
        if (g_log_fp) fclose(g_log_fp);
        return 1;
    }
    LOG_INFO("  read_waveform: %p", (void*)wave_dev->read_waveform);

    /* 分配波形缓冲区 */
    wave_buf = (uint8_t *)malloc(WAVE_BUF_SIZE);
    if (wave_buf == NULL) {
        LOG_ERROR("Memory allocation failed (size=%d)", WAVE_BUF_SIZE);
        hal_device_release(dev);
        hal_exit();
        if (g_log_fp) fclose(g_log_fp);
        return 1;
    }
    LOG_INFO("Buffer allocated: %d bytes", WAVE_BUF_SIZE);

    /* ========== 主循环 ========== */
    int success_count = 0;
    int fail_count = 0;
    int retries = 0;
    const int MAX_RETRIES = 3;
    
    LOG_INFO("Starting main loop...");
    LOG_INFO("");
    
    for (i = 0; i < max_cycles; i++) {
        LOG_INFO("------------------------------------------------------------");
        LOG_INFO("[TEST] Cycle %d/%d", i + 1, max_cycles);
        LOG_INFO("------------------------------------------------------------");

        /* 1. 采集原始波形 */
        LOG_INFO("[STEP 1] Collecting raw waveform...");
        memset(wave_buf, 0, WAVE_BUF_SIZE);
        
        struct timeval tv_start, tv_end;
        gettimeofday(&tv_start, NULL);
        
        ret = wave_dev->read_waveform(wave_dev, wave_buf, WAVE_BUF_SIZE, 1);
        
        gettimeofday(&tv_end, NULL);
        int read_time_ms = (tv_end.tv_sec - tv_start.tv_sec) * 1000 + 
                          (tv_end.tv_usec - tv_start.tv_usec) / 1000;
        
        if (ret <= 0) {
            LOG_ERROR("Waveform collection failed: ret=%ld (time: %dms)", ret, read_time_ms);
            retries++;
            if (retries >= MAX_RETRIES) {
                LOG_ERROR("Max retries (%d) reached, skipping cycle", MAX_RETRIES);
                retries = 0;
                fail_count++;
                i++;
                continue;
            }
            LOG_WARN("Retrying collection (attempt %d/%d)...", retries, MAX_RETRIES);
            usleep(100000);
            i--;
            continue;
        }
        
        retries = 0;
        LOG_INFO("Collection complete: %ld bytes in %dms", ret, read_time_ms);
        
        /* 打印波形摘要 */
        print_wave_summary(wave_buf, ret);
        
        /* 解析周波序号 */
        uint32_t cycle_seq = 0;
        if (ret > 11) {
            cycle_seq = read_le32(&wave_buf[7]);
        }

        /* 2. 连接RK3576并发送 (V2协议) */
        LOG_INFO("[STEP 2] Connecting to RK3576 and sending waveform (V2)...");
        
        int sock_fd = connect_to_server(server_ip, server_port, DEFAULT_TIMEOUT_MS);
        if (sock_fd < 0) {
            LOG_ERROR("Cannot connect to RK3576, skipping this cycle");
            fail_count++;
            usleep(interval_ms * 1000);
            continue;
        }
        
        send_seq++;  /* 递增 V2 协议序列号 */
        
        int send_ret = send_waveform_v2(sock_fd, wave_buf, ret, send_seq);
        if (send_ret != 0) {
            LOG_ERROR("Failed to send waveform (V2)");
            close(sock_fd);
            fail_count++;
            continue;
        }

        /* 3. 接收AI推理结果 (V2协议) */
        LOG_INFO("[STEP 3] Waiting for AI inference result (V2)...");
        
        ai_response_v2_t ai_result;
        int recv_ret = receive_ai_result_v2(sock_fd, &ai_result, DEFAULT_TIMEOUT_MS);
        
        close(sock_fd);
        
        if (recv_ret == 0) {
            success_count++;
            LOG_INFO("[STEP 3] AI inference result received (V2)");
            
            /* 4. 显示详细结果 */
            LOG_INFO("[STEP 4] ========== AI Inference Result (V2) ==========");
            
            if (ai_result.resp_type == RESP_OK || ai_result.resp_type == RESP_ANOMALY) {
                LOG_INFO("Response Type: %s", 
                        ai_result.resp_type == RESP_ANOMALY ? "ANOMALY DETECTED" : "NORMAL");
                
                LOG_INFO("Scores:");
                LOG_INFO("  iForest: %.4f (0=normal, 1=anomaly)", ai_result.if_score);
                LOG_INFO("  AE: %.4f", ai_result.ae_score);
                LOG_INFO("  CNN Class: %d", ai_result.cnn_class);
                LOG_INFO("  CNN Confidence: %.2f", ai_result.cnn_confidence);
                LOG_INFO("  Scene ID: %d", ai_result.scene_id);
                
                /* 根据CNN分类结果解释 */
                switch (ai_result.cnn_class) {
                    case 0:
                        LOG_INFO("  Interpretation: Normal operation");
                        break;
                    case 1:
                        LOG_INFO("  Interpretation: Voltage sag");
                        break;
                    case 2:
                        LOG_INFO("  Interpretation: Voltage swell");
                        break;
                    case 3:
                        LOG_INFO("  Interpretation: Harmonic distortion");
                        break;
                    case 4:
                        LOG_INFO("  Interpretation: Three-phase unbalance");
                        break;
                    case 5:
                        LOG_INFO("  Interpretation: Overload");
                        break;
                    case 6:
                        LOG_INFO("  Interpretation: Transient pulse");
                        break;
                    default:
                        LOG_INFO("  Interpretation: Unknown (class=%d)", ai_result.cnn_class);
                }
                
                if (ai_result.resp_type == RESP_ANOMALY) {
                    LOG_WARN("Anomaly detected!");
                } else {
                    LOG_INFO("Waveform is normal");
                }
            } else {
                LOG_ERROR("Unknown response type: %d", ai_result.resp_type);
                fail_count++;
            }
            LOG_INFO("========================================");
        } else {
            fail_count++;
            LOG_ERROR("[STEP 3] Failed to receive inference result");
        }

        LOG_INFO("");
        
        /* 等待下一个周期 */
        if (i < max_cycles - 1) {
            LOG_DEBUG("Waiting %dms before next cycle...", interval_ms);
            usleep(interval_ms * 1000);
        }
    }

    /* ========== 统计 ========== */
    LOG_INFO("============================================================");
    LOG_INFO("Test completed (V2 Protocol)!");
    LOG_INFO("  Success: %d", success_count);
    LOG_INFO("  Failed: %d", fail_count);
    LOG_INFO("  Total: %d", max_cycles);
    LOG_INFO("  Success rate: %.1f%%", (max_cycles > 0) ? (success_count * 100.0 / max_cycles) : 0);
    LOG_INFO("  Log file: %s", log_file);
    LOG_INFO("  View log: tail -f %s", log_file);
    LOG_INFO("============================================================");

    /* 清理 */
    free(wave_buf);
    hal_device_release(dev);
    hal_exit();
    
    if (g_log_fp) {
        fclose(g_log_fp);
        g_log_fp = NULL;
    }

    return (fail_count == 0) ? 0 : 1;
}
