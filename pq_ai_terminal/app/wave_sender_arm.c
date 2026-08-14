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
 * AI 响应 (63字节扩展格式，含7通道有效值):
 *   magic(4) + resp_type(1) + timestamp(8) + cycle_seq(4) + 
 *   if_score(4) + ae_score(4) + cnn_confidence(4) + cnn_class(1) + 
 *   scene_id(1) + ua_rms(4) + ub_rms(4) + uc_rms(4) +
 *   ia_rms(4) + ib_rms(4) + ic_rms(4) + iz_rms(4) + crc32(4)
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
#define WS_POINTS_PER_CYCLE 256
#define WS_CHANNEL_COUNT 7
#define WS_WAVEFORM_HEADER_SIZE 18  /* nc(2) + ppc(4) + cs(4) + ts(8) */
#define WS_WAVEFORM_DATA_SIZE (WS_CHANNEL_COUNT * WS_POINTS_PER_CYCLE * 4)  /* 7168 bytes */
#define WS_SINGLE_WAVEFORM_SIZE (WS_WAVEFORM_HEADER_SIZE + WS_WAVEFORM_DATA_SIZE)  /* 7186 bytes */
#define WAVE_BUF_SIZE (WS_SINGLE_WAVEFORM_SIZE * 20)  /* 143720 bytes buffer for 20 cycles */

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

/* AI 响应负载大小 (V2帧内的payload, 扩展格式含7通道有效值) */
#define AI_RESPONSE_PAYLOAD_SIZE 63

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

/* AI推理响应 - 63字节扩展格式 (使用packed防止对齐填充) */
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
    float    ua_rms;        /* A相电压有效值 (4) */
    float    ub_rms;        /* B相电压有效值 (4) */
    float    uc_rms;        /* C相电压有效值 (4) */
    float    ia_rms;        /* A相电流有效值 (4) */
    float    ib_rms;        /* B相电流有效值 (4) */
    float    ic_rms;        /* C相电流有效值 (4) */
    float    iz_rms;        /* 零序电流有效值 (4) */
    uint32_t crc32;         /* CRC32校验 (4) */
} ai_response_v2_t;
/* 总计: 4+1+8+4+4+4+4+1+1+4*7+4 = 63字节 */

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
    
    /* 
     * 帧格式: [header(14)][crc32(4)][payload]
     * 与 Python protocol_v2.py 的 DataFrame.decode() 一致
     */
    
    /* 先计算 CRC32 (基于 header + payload) */
    uint8_t temp_buf[WAVE_BUF_SIZE + FRAME_HEADER_SIZE];
    memcpy(temp_buf, &header, sizeof(header));
    memcpy(temp_buf + sizeof(header), wave_data, wave_len);
    crc = crc32_calc(temp_buf, sizeof(header) + wave_len);
    
    /* 按照 [header][crc][payload] 格式组装发送缓冲区 */
    memcpy(send_buf, &header, sizeof(header));
    memcpy(send_buf + sizeof(header), &crc, 4);
    memcpy(send_buf + sizeof(header) + 4, wave_data, wave_len);
    
    total_len = sizeof(header) + 4 + wave_len;  /* header + crc + payload */
    
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
    int ack_count = 0;
    const int MAX_ACK_SKIP = 10;  /* 最多跳过 10 个 ACK 帧 */
    
    LOG_INFO("Waiting for AI response (V2 frame, timeout: %dms)...", timeout_ms);
    
    /* 循环等待，跳过 ACK 帧直到收到 AI_RESULT */
    while (ack_count <= MAX_ACK_SKIP) {
        /* 设置超时 */
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        
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
        
        /* 步骤1: 读取 V2 帧头 (14字节) - 确保完整读取 */
        header_ptr = frame_buf;
        n = 0;
        while (n < FRAME_HEADER_SIZE) {
            int ret = recv(sock_fd, header_ptr + n, FRAME_HEADER_SIZE - n, 0);
            if (ret <= 0) {
                LOG_ERROR("Incomplete V2 header: got %d bytes, expected %d", n, FRAME_HEADER_SIZE);
                return -1;
            }
            n += ret;
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
    
    /* 跳过 ACK/HEARTBEAT 帧 (RK3576 先回复 ACK/HEARTBEAT，再发送 AI 结果) */
    if (cmd == CMD_ACK || cmd == 0x02) {
        ack_count++;
        LOG_DEBUG("Skipping ACK/HEARTBEAT frame (cmd=%d, seq=%u, payload_len=%u), waiting for AI_RESULT...", cmd, seq, payload_len);
        /* 读取并丢弃 CRC32 (4字节) + 负载 */
        uint8_t skip_buf[1024];
        uint32_t skip_total = 4 + payload_len;  /* CRC32 + payload */
        uint32_t skip_read = 0;
        while (skip_read < skip_total) {
            uint32_t to_read = skip_total - skip_read;
            if (to_read > sizeof(skip_buf)) to_read = sizeof(skip_buf);
            int ret = recv(sock_fd, skip_buf, to_read, 0);
            if (ret <= 0) {
                LOG_ERROR("Failed to skip frame data: read %u/%u bytes", skip_read, skip_total);
                return -1;
            }
            skip_read += ret;
        }
        LOG_DEBUG("Skipped %u bytes (CRC32 + payload)", skip_total);
        continue;  /* 继续循环等待 AI_RESULT */
    }
    
    /* 验证命令类型 - 必须是 AI_RESULT */
    if (cmd != CMD_AI_RESULT) {
        LOG_ERROR("Not an AI result frame: cmd=%d (expected %d)", cmd, CMD_AI_RESULT);
        return -1;
    }
    
    /* 验证负载长度 */
    if (payload_len != AI_RESPONSE_PAYLOAD_SIZE) {
        LOG_ERROR("Unexpected payload length: %u (expected %d)", payload_len, AI_RESPONSE_PAYLOAD_SIZE);
        return -1;
    }
    
    /* 步骤2: 读取 CRC32 (4字节) - 确保完整读取 */
    crc_ptr = frame_buf + FRAME_HEADER_SIZE;
    n = 0;
    while (n < 4) {
        int ret = recv(sock_fd, crc_ptr + n, 4 - n, 0);
        if (ret <= 0) {
            LOG_ERROR("Incomplete CRC32: got %d bytes, expected 4", n);
            return -1;
        }
        n += ret;
    }
    
    /* 步骤3: 读取负载 (AI响应数据, 35字节) - 确保完整读取 */
    payload_ptr = frame_buf + FRAME_HEADER_SIZE + 4;
    n = 0;
    while (n < (int)payload_len) {
        int ret = recv(sock_fd, payload_ptr + n, payload_len - n, 0);
        if (ret <= 0) {
            LOG_ERROR("Incomplete AI payload: got %d bytes, expected %u", n, payload_len);
            return -1;
        }
        n += ret;
    }
    
    LOG_DEBUG("Received V2 frame: header=%d+crc=4+payload=%u bytes", FRAME_HEADER_SIZE, payload_len);
    
    /* 步骤4: 验证 V2 帧 CRC32 (基于 header + payload, 不包括 CRC32 本身) */
    received_crc = read_le32(crc_ptr);
    {
        /* 构建临时缓冲区: [header][payload] */
        uint8_t crc_check_buf[FRAME_HEADER_SIZE + AI_RESPONSE_PAYLOAD_SIZE + 256];
        memcpy(crc_check_buf, frame_buf, FRAME_HEADER_SIZE);
        memcpy(crc_check_buf + FRAME_HEADER_SIZE, payload_ptr, payload_len);
        calc_crc = crc32_calc(crc_check_buf, FRAME_HEADER_SIZE + payload_len);
    }
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
        memcpy(&result->ua_rms, p, 4); p += 4;
        memcpy(&result->ub_rms, p, 4); p += 4;
        memcpy(&result->uc_rms, p, 4); p += 4;
        memcpy(&result->ia_rms, p, 4); p += 4;
        memcpy(&result->ib_rms, p, 4); p += 4;
        memcpy(&result->ic_rms, p, 4); p += 4;
        memcpy(&result->iz_rms, p, 4); p += 4;
        result->crc32 = read_le32(p); p += 4;
    }
    
    /* 验证 AI 响应魔数 (负载内部) */
    if (result->magic != 0x57415645) {
        LOG_ERROR("Invalid AI response magic: 0x%08X", result->magic);
        return -1;
    }
    
    /* 验证 AI 响应 CRC32 (基于负载前 59 字节, 不包括 crc32 字段) */
    received_crc = result->crc32;
    calc_crc = crc32_calc(payload_ptr, AI_RESPONSE_PAYLOAD_SIZE - 4);
    if (received_crc != calc_crc) {
        LOG_ERROR("AI response CRC32 mismatch: received=0x%08X, calculated=0x%08X", received_crc, calc_crc);
        return -1;
    }
    
    /* 打印响应内容 (含7通道有效值) */
    LOG_INFO("AI Response received (V2 frame, seq=%u, %d bytes):", seq, AI_RESPONSE_PAYLOAD_SIZE);
    LOG_INFO("  Magic: 0x%08X", result->magic);
    LOG_INFO("  Response Type: %s (resp_type=%d)", 
            result->resp_type == RESP_OK ? "OK" : "ANOMALY", result->resp_type);
    LOG_INFO("  Timestamp: %llu us", (unsigned long long)result->timestamp);
    LOG_INFO("  Cycle Seq: %u", result->cycle_seq);
    LOG_INFO("  iForest Score: %.4f", result->if_score);
    LOG_INFO("  AE Score: %.4f", result->ae_score);
    LOG_INFO("  CNN Class: %d", result->cnn_class);
    LOG_INFO("  CNN Confidence: %.4f", result->cnn_confidence);
    LOG_INFO("  Scene ID: %d", result->scene_id);
    LOG_INFO("  电压有效值: UA=%.3fV, UB=%.3fV, UC=%.3fV", 
             result->ua_rms, result->ub_rms, result->uc_rms);
    LOG_INFO("  电流有效值: IA=%.3fA, IB=%.3fA, IC=%.3fA, IZ=%.3fA",
             result->ia_rms, result->ib_rms, result->ic_rms, result->iz_rms);
    LOG_INFO("  CRC32: 0x%08X", result->crc32);
    
    return 0;
    }
    
    /* 超过最大 ACK 跳过次数，返回错误 */
    LOG_ERROR("Too many ACK frames skipped (%d), no AI_RESULT received", ack_count);
    return -1;
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

/* ========== 波形数据格式转换 ========== */
/*
 * T536 原始格式: [0x68,0x36][frame_len(4)][frame_seq(1)][pad(3)][cycle_seq(4)][timestamp(10)][channels(7168)]
 * RK3576 期望格式: [nc(2)][ppc(4)][cs(4)][ts(8)][channels(7168)]
 */
static int convert_waveform_format(const uint8_t *raw_data, int raw_len, 
                                    uint8_t *out_data, int out_buf_size)
{
    if (raw_len < 26) {  /* 最小帧头: 2+4+1+3+4+10 = 24 */
        LOG_ERROR("Raw data too short: %d bytes", raw_len);
        return -1;
    }
    
    /* 查找帧头 */
    int header_offset = -1;
    for (int i = 0; i < raw_len - 1; i++) {
        if (raw_data[i] == 0x68 && raw_data[i + 1] == 0x36) {
            header_offset = i;
            break;
        }
    }
    
    if (header_offset < 0) {
        LOG_ERROR("No valid frame header found in raw data");
        return -1;
    }
    
    /* 解析 T536 原始帧头 */
    int off = header_offset;
    uint32_t frame_len = read_le32(&raw_data[off + 2]);
    uint8_t frame_seq = raw_data[off + 6];
    
    /* 跳过 frame_seq(1) + padding(3) = 4字节 */
    off = header_offset + 7;
    
    uint32_t cycle_seq = read_le32(&raw_data[off]);
    off += 4;
    
    /* 解析时间戳: year(1), month(1), day(1), hour(1), minute(1), second(1), us(4) */
    uint8_t year = raw_data[off];
    uint8_t month = raw_data[off + 1];
    uint8_t day = raw_data[off + 2];
    uint8_t hour = raw_data[off + 3];
    uint8_t minute = raw_data[off + 4];
    uint8_t second = raw_data[off + 5];
    uint32_t us_part = read_le32(&raw_data[off + 6]);
    off += 10;
    
    /* 将时间戳转换为微秒 (简化处理: 使用年月日时分秒 + 微秒部分) */
    uint64_t timestamp_us = (uint64_t)(year + 2000) * 365LL * 24 * 3600 * 1000000ULL
                           + (uint64_t)month * 30LL * 24 * 3600 * 1000000ULL
                           + (uint64_t)day * 24LL * 3600 * 1000000ULL
                           + (uint64_t)hour * 3600LL * 1000000ULL
                           + (uint64_t)minute * 60LL * 1000000ULL
                           + (uint64_t)second * 1000000ULL
                           + us_part;
    
    /* 通道数据起始位置 (跳过帧头后的通道数据) */
    int channel_offset = off;
    
    /* 验证数据完整性 */
    int expected_size = (channel_offset - header_offset) + WS_WAVEFORM_DATA_SIZE;
    if (raw_len - header_offset < expected_size) {
        LOG_ERROR("Raw data incomplete: need %d, have %d", 
                  expected_size, raw_len - header_offset);
        return -1;
    }
    
    /* 检查输出缓冲区大小 */
    int out_size = WS_SINGLE_WAVEFORM_SIZE;
    if (out_buf_size < out_size) {
        LOG_ERROR("Output buffer too small: need %d, have %d", out_size, out_buf_size);
        return -1;
    }
    
    /* 构建 RK3576 格式波形数据 */
    off = 0;
    uint16_t nc = WS_CHANNEL_COUNT;
    uint32_t ppc = WS_POINTS_PER_CYCLE;
    
    memcpy(out_data + off, &nc, 2); off += 2;
    memcpy(out_data + off, &ppc, 4); off += 4;
    memcpy(out_data + off, &cycle_seq, 4); off += 4;
    memcpy(out_data + off, &timestamp_us, 8); off += 8;
    
    /* 复制通道数据 */
    memcpy(out_data + off, raw_data + channel_offset, WS_WAVEFORM_DATA_SIZE);
    off += WS_WAVEFORM_DATA_SIZE;
    
    LOG_DEBUG("Converted waveform: nc=%d, ppc=%d, seq=%u, ts=%llu, channels=%d bytes",
              nc, ppc, cycle_seq, (unsigned long long)timestamp_us, WS_WAVEFORM_DATA_SIZE);
    
    return out_size;
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
    uint8_t *converted_buf = NULL;
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

    /* 分配格式转换缓冲区 */
    converted_buf = (uint8_t *)malloc(WS_SINGLE_WAVEFORM_SIZE);
    if (converted_buf == NULL) {
        LOG_ERROR("Memory allocation failed for converted_buf (size=%d)", WS_SINGLE_WAVEFORM_SIZE);
        free(wave_buf);
        hal_device_release(dev);
        hal_exit();
        if (g_log_fp) fclose(g_log_fp);
        return 1;
    }
    LOG_INFO("Conversion buffer allocated: %d bytes", WS_SINGLE_WAVEFORM_SIZE);

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

        /* 1.5 格式转换: T536原始格式 -> RK3576期望格式 */
        LOG_INFO("[STEP 1.5] Converting waveform format...");
        int converted_len = convert_waveform_format(wave_buf, ret, converted_buf, WS_SINGLE_WAVEFORM_SIZE);
        if (converted_len <= 0) {
            LOG_ERROR("Failed to convert waveform format, skipping this cycle");
            fail_count++;
            usleep(interval_ms * 1000);
            continue;
        }
        LOG_INFO("Format converted: %d -> %d bytes", ret, converted_len);

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
        
        int send_ret = send_waveform_v2(sock_fd, converted_buf, converted_len, send_seq);
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
            
            /* 4. 显示详细结果 (含7通道有效值) */
            LOG_INFO("[STEP 4] ========== AI Inference Result (V2) ==========");
            
            if (ai_result.resp_type == RESP_OK || ai_result.resp_type == RESP_ANOMALY) {
                LOG_INFO("Response Type: %s", 
                        ai_result.resp_type == RESP_ANOMALY ? "ANOMALY DETECTED" : "NORMAL");
                
                LOG_INFO("Scores:");
                LOG_INFO("  iForest: %.4f (0=normal, 1=anomaly)", ai_result.if_score);
                LOG_INFO("  AE: %.4f", ai_result.ae_score);
                LOG_INFO("  CNN Class: %d", ai_result.cnn_class);
                LOG_INFO("  CNN Confidence: %.4f", ai_result.cnn_confidence);
                LOG_INFO("  Scene ID: %d", ai_result.scene_id);
                
                /* 显示 7 通道有效值 */
                LOG_INFO("有效值 (RMS):");
                LOG_INFO("  电压: UA=%.3fV, UB=%.3fV, UC=%.3fV",
                        ai_result.ua_rms, ai_result.ub_rms, ai_result.uc_rms);
                LOG_INFO("  电流: IA=%.3fA, IB=%.3fA, IC=%.3fA, IZ=%.3fA",
                        ai_result.ia_rms, ai_result.ib_rms, ai_result.ic_rms, ai_result.iz_rms);
                
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
                    case 7:
                        LOG_INFO("  Interpretation: Three-phase loss");
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
    free(converted_buf);
    hal_device_release(dev);
    hal_exit();
    
    if (g_log_fp) {
        fclose(g_log_fp);
        g_log_fp = NULL;
    }

    return (fail_count == 0) ? 0 : 1;
}
