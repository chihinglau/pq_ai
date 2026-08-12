/**
 * @file wave_sender_arm.c
 * @brief T536 波形采集发送端 - 只负责采集原始波形并发送给RK3576
 * 
 * 正确的业务流程:
 * 1. T536采集原始波形数据
 * 2. 通过USB ECM (TCP) 发送给RK3576
 * 3. RK3576接收、特征提取、AI推理
 * 4. RK3576返回推理结果给T536
 * 
 * 本程序只负责: 采集 → 发送 → 接收结果
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
typedef long int32;
typedef unsigned long uint32;

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

/* 协议头标识 */
#define PROTO_MAGIC 0x57415645  /* "WAVE" */
#define PROTO_VERSION 1

/* 命令类型 */
#define CMD_SEND_WAVEFORM 1
#define CMD_RESPONSE 2

/* 响应类型 */
#define RESP_OK 0
#define RESP_ERROR 1

/* USB ECM 配置 */
#define DEFAULT_SERVER_IP "192.168.100.1"
#define DEFAULT_SERVER_PORT 9090
#define DEFAULT_TIMEOUT_MS 5000

/* ========== 协议结构 ========== */
/* 传输头 - 24字节 (使用packed防止对齐填充) */
typedef struct __attribute__((packed)) {
    uint32_t magic;         /* 魔数: 0x57415645 ("WAVE") (4) */
    uint8_t  version;       /* 协议版本 (1) */
    uint8_t  cmd;           /* 命令类型 (1) */
    uint16_t reserved;      /* 预留 (2) */
    uint32_t cycle_seq;     /* 周波序号 (4) */
    uint32_t timestamp_sec; /* 时间戳秒 (4) */
    uint32_t timestamp_us;  /* 时间戳微秒 (4) */
    uint32_t data_len;      /* 数据长度 (4) */
} proto_header_t;
/* 总计: 4+1+1+2+4+4+4+4 = 24字节 */

/* AI推理响应 - 48字节 (使用packed防止对齐填充) */
typedef struct __attribute__((packed)) {
    uint32_t magic;         /* 魔数 (4) */
    uint8_t  resp_type;     /* 响应类型 (1) */
    uint8_t  reserved[3];   /* 预留 (3) */
    float    if_score;      /* iForest异常得分 (4) */
    float    ae_score;      /* AE重构误差 (4) */
    int32_t  cnn_class;     /* CNN事件分类 (4) */
    float    cnn_confidence;/* CNN置信度 (4) */
    int32_t  latency_ms;    /* 服务器处理延迟 (4) */
    uint32_t result_code;   /* 结果码 (4) */
    char     result_desc[16];/* 结果描述 (16) */
} ai_response_t;
/* 总计: 4+1+3+4+4+4+4+4+4+16 = 48字节 */

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

/* 打印字节数组 */
static void print_bytes(const uint8_t *data, int len, int bytes_per_line) {
    int i;
    for (i = 0; i < len; i++) {
        printf("%02X ", data[i]);
        if ((i + 1) % bytes_per_line == 0) printf("\n  ");
    }
    if (len % bytes_per_line != 0) printf("\n");
}

/* ========== 函数声明 ========== */
static int connect_to_server(const char *ip, int port, int timeout_ms);
static int send_waveform(int sock_fd, uint8_t *wave_data, int wave_len, 
                          uint32_t cycle_seq, uint32_t ts_sec, uint32_t ts_us);
static int receive_ai_result(int sock_fd, ai_response_t *result, int timeout_ms);
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

/* ========== 发送原始波形数据 ========== */
static int send_waveform(int sock_fd, uint8_t *wave_data, int wave_len,
                         uint32_t cycle_seq, uint32_t ts_sec, uint32_t ts_us)
{
    proto_header_t header;
    uint8_t send_buf[WAVE_BUF_SIZE + sizeof(proto_header_t)];
    int total_len;
    int sent;
    
    /* 构建协议头 */
    header.magic = PROTO_MAGIC;
    header.version = PROTO_VERSION;
    header.cmd = CMD_SEND_WAVEFORM;
    header.reserved = 0;
    header.cycle_seq = cycle_seq;
    header.timestamp_sec = ts_sec;
    header.timestamp_us = ts_us;
    header.data_len = wave_len;
    
    /* 组合头和数据 */
    memcpy(send_buf, &header, sizeof(header));
    memcpy(send_buf + sizeof(header), wave_data, wave_len);
    total_len = sizeof(header) + wave_len;
    
    LOG_INFO("Sending waveform: cycle_seq=%u, total_len=%d bytes", cycle_seq, total_len);
    LOG_DEBUG("Timestamp: %u.%06u", ts_sec, ts_us);
    LOG_DEBUG("Header: magic=0x%08X, ver=%d, cmd=%d, data_len=%u",
             header.magic, header.version, header.cmd, header.data_len);
    
    /* 验证魔数 */
    if (header.magic != PROTO_MAGIC) {
        LOG_ERROR("Invalid magic in header: 0x%08X (expected 0x%08X)", header.magic, PROTO_MAGIC);
        return -1;
    }
    
    /* 打印波形数据前32字节（包含帧头） */
    LOG_DEBUG("First 32 bytes of waveform data:");
    LOG_DEBUG("  %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
             wave_data[0], wave_data[1], wave_data[2], wave_data[3],
             wave_data[4], wave_data[5], wave_data[6], wave_data[7],
             wave_data[8], wave_data[9], wave_data[10], wave_data[11],
             wave_data[12], wave_data[13], wave_data[14], wave_data[15],
             wave_data[16], wave_data[17], wave_data[18], wave_data[19],
             wave_data[20], wave_data[21], wave_data[22], wave_data[23],
             wave_data[24], wave_data[25], wave_data[26], wave_data[27],
             wave_data[28], wave_data[29], wave_data[30], wave_data[31]);
    
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

/* ========== 接收AI推理结果 ========== */
static int receive_ai_result(int sock_fd, ai_response_t *result, int timeout_ms)
{
    struct timeval tv;
    fd_set readfds;
    int ret;
    int n;
    
    /* 设置超时 */
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    
    LOG_INFO("Waiting for AI response (timeout: %dms, expected: %zu bytes)...", 
            timeout_ms, sizeof(*result));
    
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
    
    /* 读取响应 */
    n = recv(sock_fd, result, sizeof(*result), 0);
    if (n < (int)sizeof(ai_response_t)) {
        LOG_ERROR("Incomplete response: got %d bytes, expected %zu bytes", n, sizeof(*result));
        if (n > 0) {
            LOG_DEBUG("Received data:");
            for (int i = 0; i < n; i++) {
                printf("  [%02d]: %02X", i, ((uint8_t*)result)[i]);
                if ((i + 1) % 8 == 0) printf("\n");
            }
            printf("\n");
        }
        return -1;
    }
    
    LOG_DEBUG("Received %d bytes of AI response", n);
    
    /* 验证魔数 */
    if (result->magic != PROTO_MAGIC) {
        LOG_ERROR("Invalid magic in response: 0x%08X (expected 0x%08X)", result->magic, PROTO_MAGIC);
        LOG_DEBUG("Full response bytes:");
        for (int i = 0; i < (int)sizeof(*result); i++) {
            printf("  [%02d]: %02X", i, ((uint8_t*)result)[i]);
            if ((i + 1) % 8 == 0) printf("\n");
        }
        printf("\n");
        return -1;
    }
    
    /* 打印响应内容 */
    LOG_INFO("AI Response received:");
    LOG_INFO("  Magic: 0x%08X", result->magic);
    LOG_INFO("  Response Type: %s (resp_type=%d)", 
            result->resp_type == RESP_OK ? "OK" : "ERROR", result->resp_type);
    LOG_INFO("  iForest Score: %.4f", result->if_score);
    LOG_INFO("  AE Score: %.4f", result->ae_score);
    LOG_INFO("  CNN Class: %d", result->cnn_class);
    LOG_INFO("  CNN Confidence: %.2f", result->cnn_confidence);
    LOG_INFO("  Latency: %d ms", result->latency_ms);
    LOG_INFO("  Result Code: %u", result->result_code);
    LOG_INFO("  Result Desc: %.16s", result->result_desc);
    
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
            
            LOG_DEBUG("Frame header hex dump:");
            LOG_DEBUG("  %02X %02X %02X %02X %02X %02X %02X %02X",
                     wave_data[i], wave_data[i+1], wave_data[i+2], wave_data[i+3],
                     wave_data[i+4], wave_data[i+5], wave_data[i+6], wave_data[i+7]);
            break;
        }
    }
    
    if (!header_found) {
        LOG_WARN("No valid frame header found in waveform data!");
        LOG_WARN("First 32 bytes:");
        for (int i = 0; i < 32 && i < wave_len; i++) {
            printf("  [%02d]: %02X (%d)", i, wave_data[i], wave_data[i]);
            if ((i + 1) % 4 == 0) printf(" |");
            printf("\n");
        }
    }
}

/* ========== 主函数 ========== */
int main(int argc, char *argv[])
{
    int max_cycles = 5;
    int interval_ms = 500;
    const char *server_ip = DEFAULT_SERVER_IP;
    int server_port = DEFAULT_SERVER_PORT;
    const char *log_file = "wave_sender.log";
    
    int i, ret;
    WAVEFORM_SAMPLER_DEVICE_T *wave_dev = NULL;
    HW_DEVICE *dev = NULL;
    uint8_t *wave_buf = NULL;

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
            printf("  --log FILE       Log file (default: wave_sender.log)\n");
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
    LOG_INFO("T536 Wave Collector & AI Sender");
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
        LOG_ERROR("  4. Run wave_export_arm first to verify device access");
        hal_exit();
        if (g_log_fp) fclose(g_log_fp);
        return 1;
    }
    wave_dev = (WAVEFORM_SAMPLER_DEVICE_T *)dev;
    LOG_INFO("Device obtained successfully!");
    LOG_INFO("  pModule: %p", (void*)wave_dev->base.pModule);
    LOG_INFO("  nVer: %ld", wave_dev->base.nVer);
    LOG_INFO("  szDeviceID: %s", wave_dev->base.szDeviceID ? wave_dev->base.szDeviceID : "(null)");
    
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
        LOG_DEBUG("cycle_seq = %u", cycle_seq);

        /* 2. 连接RK3576并发送 */
        LOG_INFO("[STEP 2] Connecting to RK3576 and sending waveform...");
        
        int sock_fd = connect_to_server(server_ip, server_port, DEFAULT_TIMEOUT_MS);
        if (sock_fd < 0) {
            LOG_ERROR("Cannot connect to RK3576, skipping this cycle");
            fail_count++;
            usleep(interval_ms * 1000);
            continue;
        }
        
        struct timeval ts_now;
        gettimeofday(&ts_now, NULL);
        
        int send_ret = send_waveform(sock_fd, wave_buf, ret, 
                                     cycle_seq, ts_now.tv_sec, ts_now.tv_usec);
        if (send_ret != 0) {
            LOG_ERROR("Failed to send waveform");
            close(sock_fd);
            fail_count++;
            continue;
        }

        /* 3. 接收AI推理结果 */
        LOG_INFO("[STEP 3] Waiting for AI inference result...");
        
        ai_response_t ai_result;
        int recv_ret = receive_ai_result(sock_fd, &ai_result, DEFAULT_TIMEOUT_MS);
        
        close(sock_fd);
        
        if (recv_ret == 0) {
            success_count++;
            LOG_INFO("[STEP 3] ✅ AI inference result received");
            
            /* 4. 显示详细结果 */
            LOG_INFO("[STEP 4] ========== AI Inference Result ==========");
            
            if (ai_result.resp_type == RESP_OK) {
                LOG_INFO("Status: OK");
                
                LOG_INFO("Scores:");
                LOG_INFO("  iForest: %.4f (0=normal, 1=anomaly)", ai_result.if_score);
                LOG_INFO("  AE: %.4f", ai_result.ae_score);
                LOG_INFO("  CNN Class: %d", ai_result.cnn_class);
                LOG_INFO("  CNN Confidence: %.2f", ai_result.cnn_confidence);
                
                /* 根据CNN分类结果解释 */
                switch (ai_result.cnn_class) {
                    case 0:
                        LOG_INFO("  Interpretation: Normal operation");
                        break;
                    case 1:
                        LOG_INFO("  Interpretation: Slight anomaly");
                        break;
                    case 2:
                        LOG_INFO("  Interpretation: Voltage sag");
                        break;
                    case 3:
                        LOG_INFO("  Interpretation: Single phase open circuit");
                        break;
                    case 4:
                        LOG_INFO("  Interpretation: Overvoltage");
                        break;
                    case 5:
                        LOG_INFO("  Interpretation: Undervoltage");
                        break;
                    case 6:
                        LOG_INFO("  Interpretation: Other fault");
                        break;
                    default:
                        LOG_INFO("  Interpretation: Unknown (class=%d)", ai_result.cnn_class);
                }
                
                if (ai_result.result_code > 0) {
                    LOG_WARN("⚠️ Anomaly detected!");
                    LOG_WARN("  Result code: %u", ai_result.result_code);
                    LOG_WARN("  Description: %s", ai_result.result_desc);
                } else {
                    LOG_INFO("✅ Waveform is normal");
                }
                
                LOG_INFO("Processing latency: %dms", ai_result.latency_ms);
            } else {
                LOG_ERROR("Status: ERROR (resp_type=%d)", ai_result.resp_type);
                LOG_ERROR("AI inference returned error");
                fail_count++;
            }
            LOG_INFO("========================================");
        } else {
            fail_count++;
            LOG_ERROR("[STEP 3] ❌ Failed to receive inference result");
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
    LOG_INFO("Test completed!");
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
