/**
 * @file wave_sender_arm_protocol_test.c
 * @brief T536 波形采集发送端 - V2 协议测试版 (不依赖HAL)
 * 
 * 仅测试 V2 协议通信功能，验证:
 *   1. V2 帧格式正确性
 *   2. CRC32 校验算法
 *   3. AI 响应解析
 * 
 * 用法: wave_sender_test [options]
 *   --cycles N       发送周期数 (默认5)
 *   --interval MS    发送间隔毫秒 (默认500)
 *   --server IP      RK3576服务器IP (默认192.168.100.1)
 *   --port PORT      RK3576服务端口 (默认9090)
 *   --log FILE       日志文件名 (default: wave_sender_test.log)
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
/* 注意: 使用标准整数类型确保跨平台一致性 */
/* int32_t, uint32_t, uint64_t 已在 <stdint.h> 中定义 */
/* 这里定义别名以兼容原有代码 */
typedef int32_t int32;
typedef uint32_t uint32;
typedef uint64_t uint64;

/* ========== 常量 ========== */
#define WS_POINTS_PER_CYCLE 256
#define WS_CHANNEL_COUNT 7
#define WS_WAVEFORM_HEADER_SIZE 18  /* nc(2) + ppc(4) + cs(4) + ts(8) */
#define WS_WAVEFORM_DATA_SIZE (WS_CHANNEL_COUNT * WS_POINTS_PER_CYCLE * 4)  /* 7168 bytes */
#define WS_SINGLE_WAVEFORM_SIZE (WS_WAVEFORM_HEADER_SIZE + WS_WAVEFORM_DATA_SIZE)  /* 7186 bytes */

/* 波形参数 (与 HT7627S 实际采样一致) */
#define SAMPLE_RATE 12800.0f    /* 12.8 kHz */
#define FREQ 50.0f              /* 工频 50 Hz */
#define ANGULAR_FREQ (2.0f * 3.14159265f * FREQ)  /* 314.159 rad/s */
#define VOLTAGE_PEAK (220.0f * 1.41421f)  /* 311.12V (220V RMS × √2) */
#define CURRENT_PEAK_NORMAL 7.0f  /* 正常负载电流峰值 (~5A RMS) */
#define CURRENT_PEAK_FAULT 21.0f  /* 故障电流峰值 (~15A RMS) */

/* 异常场景枚举 */
#define SCENARIO_NORMAL 0
#define SCENARIO_SINGLE_OPEN 1   /* A相正常, B/C相开路 */
#define SCENARIO_VOLTAGE_SAG 2   /* 三相电压暂降 */
#define SCENARIO_HARMONIC 3      /* 含谐波畸变 */
#define SCENARIO_THREE_PHASE_UNBALANCE 4  /* 三相不平衡 */

static int g_scenario = SCENARIO_SINGLE_OPEN;  /* 默认使用异常场景验证 AI 检测 */

/* V2 协议常量 */
#define PROTO_MAGIC 0x57415632  /* "WV2" - Wave Protocol V2 */
#define PROTO_VERSION 2

/* 命令类型 */
#define CMD_WAVEFORM 0x01
#define CMD_ACK 0x03
#define CMD_AI_RESULT 0x07

/* 响应类型 */
#define RESP_OK 0
#define RESP_ANOMALY 2

/* 帧头大小 (不包括CRC32) */
#define FRAME_HEADER_SIZE 14

/* AI 响应负载大小 */
#define AI_RESPONSE_PAYLOAD_SIZE 35

/* USB ECM 配置 */
#define DEFAULT_SERVER_IP "192.168.100.1"
#define DEFAULT_SERVER_PORT 9090
#define DEFAULT_TIMEOUT_MS 5000

/* ========== CRC32 实现 (与 zlib.crc32 兼容) ========== */
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
typedef struct __attribute__((packed)) {
    uint32_t magic;         /* 魔数: 0x57415632 ("WV2") (4) */
    uint8_t  version;       /* 协议版本 (1) */
    uint8_t  cmd;           /* 命令类型 (1) */
    uint32_t seq;           /* 序列号 (4) */
    uint32_t payload_len;   /* 负载长度 (4) */
} proto_header_v2_t;

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

/* ========== 日志系统 ========== */
static FILE *g_log_fp = NULL;
static int g_log_level = 3;  /* 0=ERROR, 1=WARN, 2=INFO, 3=DEBUG */

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

/* 生成真实波形数据 (模拟 HT7627S 硬件输出)
 * 
 * 通道定义:
 *   ch0: Ua (A相电压)   220V RMS, 50Hz
 *   ch1: Ub (B相电压)   220V RMS, 50Hz, 相位 -120°
 *   ch2: Uc (C相电压)   220V RMS, 50Hz, 相位 +120°
 *   ch3: Ia (A相电流)   与电压同相 (阻性负载)
 *   ch4: Ib (B相电流)   
 *   ch5: Ic (C相电流)
 *   ch6: U0 (零序电压)  三相电压之和/√3
 *
 * 支持场景:
 *   SCENARIO_NORMAL:           正常三相平衡
 *   SCENARIO_SINGLE_OPEN:      A相正常, B/C相开路 (模拟之前的测试)
 *   SCENARIO_VOLTAGE_SAG:      三相电压骤降至 140V RMS
 *   SCENARIO_HARMONIC:         含 3/5/7 次谐波畸变
 *   SCENARIO_THREE_PHASE_UNBALANCE: 三相电压不平衡
 */
static float calc_rms(const float *data, int len)
{
    float sum = 0.0f;
    int i;
    for (i = 0; i < len; i++) {
        sum += data[i] * data[i];
    }
    return sqrtf(sum / (float)len);
}

static void generate_test_waveform(uint8_t *wave_data, int wave_size, uint32_t cycle_seq)
{
    int ch, i;
    float val;
    uint32_t raw;
    int offset;
    uint16_t nc = WS_CHANNEL_COUNT;
    uint32_t ppc = WS_POINTS_PER_CYCLE;
    uint32_t cs = cycle_seq;
    uint64_t ts;
    struct timeval tv;
    
    memset(wave_data, 0, wave_size);
    
    /* 获取精确时间戳 */
    gettimeofday(&tv, NULL);
    ts = (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
    
    /* 写入元数据头部 (V2 协议 Payload 格式) */
    offset = 0;
    memcpy(wave_data + offset, &nc, 2); offset += 2;      /* nc: 通道数 (7) */
    memcpy(wave_data + offset, &ppc, 4); offset += 4;     /* ppc: 每周期采样点数 (256) */
    memcpy(wave_data + offset, &cs, 4); offset += 4;      /* cs: 周期序号 */
    memcpy(wave_data + offset, &ts, 8); offset += 8;      /* ts: 时间戳 (微秒) */
    
    /* 
     * 生成真实波形数据
     * 
     * 采样角度: θ = 2π × i / 256 (i = 0~255)
     * 基波: sin(θ) 
     * 三相相位差: 120° (2π/3)
     */
    for (ch = 0; ch < WS_CHANNEL_COUNT; ch++) {
        for (i = 0; i < WS_POINTS_PER_CYCLE; i++) {
            float theta = 2.0f * 3.14159265f * (float)i / 256.0f;
            float phase_shift = 0.0f;
            float amplitude = 0.0f;
            
            /* 根据通道和场景计算波形 */
            switch (ch) {
                case 0:  /* Ua - A相电压 */
                    phase_shift = 0.0f;
                    if (g_scenario == SCENARIO_SINGLE_OPEN) {
                        amplitude = VOLTAGE_PEAK;  /* A相正常 311V peak */
                    } else if (g_scenario == SCENARIO_VOLTAGE_SAG) {
                        amplitude = VOLTAGE_PEAK * 0.636f;  /* 140V RMS (140*√2=198V peak) */
                    } else {
                        amplitude = VOLTAGE_PEAK;
                    }
                    break;
                    
                case 1:  /* Ub - B相电压 */
                    phase_shift = -2.0f * 3.14159265f / 3.0f;  /* -120° */
                    if (g_scenario == SCENARIO_SINGLE_OPEN) {
                        amplitude = 0.01f;  /* B相开路 ≈ 0V */
                    } else if (g_scenario == SCENARIO_VOLTAGE_SAG) {
                        amplitude = VOLTAGE_PEAK * 0.636f;
                    } else if (g_scenario == SCENARIO_THREE_PHASE_UNBALANCE) {
                        amplitude = VOLTAGE_PEAK * 0.85f;  /* B相偏低 187V RMS */
                    } else {
                        amplitude = VOLTAGE_PEAK;
                    }
                    break;
                    
                case 2:  /* Uc - C相电压 */
                    phase_shift = 2.0f * 3.14159265f / 3.0f;  /* +120° */
                    if (g_scenario == SCENARIO_SINGLE_OPEN) {
                        amplitude = 0.01f;  /* C相开路 ≈ 0V */
                    } else if (g_scenario == SCENARIO_VOLTAGE_SAG) {
                        amplitude = VOLTAGE_PEAK * 0.636f;
                    } else if (g_scenario == SCENARIO_THREE_PHASE_UNBALANCE) {
                        amplitude = VOLTAGE_PEAK * 1.15f;  /* C相偏高 253V RMS */
                    } else {
                        amplitude = VOLTAGE_PEAK;
                    }
                    break;
                    
                case 3:  /* Ia - A相电流 */
                    phase_shift = 0.0f;  /* 与电压同相 (阻性负载) */
                    if (g_scenario == SCENARIO_SINGLE_OPEN) {
                        amplitude = CURRENT_PEAK_NORMAL;  /* A相正常电流 */
                    } else if (g_scenario == SCENARIO_HARMONIC) {
                        amplitude = CURRENT_PEAK_FAULT;  /* 谐波时电流增大 */
                    } else {
                        amplitude = CURRENT_PEAK_NORMAL;
                    }
                    break;
                    
                case 4:  /* Ib - B相电流 */
                    phase_shift = -2.0f * 3.14159265f / 3.0f;
                    if (g_scenario == SCENARIO_SINGLE_OPEN) {
                        amplitude = 0.01f;  /* B相开路无电流 */
                    } else {
                        amplitude = CURRENT_PEAK_NORMAL;
                    }
                    break;
                    
                case 5:  /* Ic - C相电流 */
                    phase_shift = 2.0f * 3.14159265f / 3.0f;
                    if (g_scenario == SCENARIO_SINGLE_OPEN) {
                        amplitude = 0.01f;  /* C相开路无电流 */
                    } else {
                        amplitude = CURRENT_PEAK_NORMAL;
                    }
                    break;
                    
                case 6:  /* U0 - 零序电压 */
                    /* U0 = (Ua + Ub + Uc) / √3 */
                    {
                        float ua = VOLTAGE_PEAK * sinf(theta + 0.0f);
                        float ub = (g_scenario == SCENARIO_SINGLE_OPEN) ? 
                                   0.01f * sinf(theta + phase_shift) :
                                   VOLTAGE_PEAK * sinf(theta + phase_shift);
                        float uc = (g_scenario == SCENARIO_SINGLE_OPEN) ? 
                                   0.01f * sinf(theta - phase_shift) :
                                   VOLTAGE_PEAK * sinf(theta - phase_shift);
                        val = (ua + ub + uc) / 1.73205f;  /* /√3 */
                    }
                    /* 添加微小噪声 */
                    val += (float)(rand() % 20 - 10) * 0.005f;
                    memcpy(&raw, &val, 4);
                    offset = WS_WAVEFORM_HEADER_SIZE + ch * WS_POINTS_PER_CYCLE * 4 + i * 4;
                    if (offset + 4 <= wave_size) {
                        memcpy(wave_data + offset, &raw, 4);
                    }
                    continue;  /* 跳过后面的通用计算 */
            }
            
            /* 基波: amplitude * sin(θ + phase_shift) */
            val = amplitude * sinf(theta + phase_shift);
            
            /* 添加谐波畸变 (仅在 HARMONIC 场景) */
            if (g_scenario == SCENARIO_HARMONIC && ch <= 2) {
                val += amplitude * 0.15f * sinf(3.0f * theta + phase_shift);  /* 3次谐波 15% */
                val += amplitude * 0.08f * sinf(5.0f * theta + phase_shift);  /* 5次谐波 8% */
                val += amplitude * 0.05f * sinf(7.0f * theta + phase_shift);  /* 7次谐波 5% */
            }
            
            /* 添加微小量化噪声 (模拟 ADC 量化误差) */
            val += (float)(rand() % 20 - 10) * 0.01f;
            
            /* 写入波形数据 (小端序 float32) */
            memcpy(&raw, &val, 4);
            offset = WS_WAVEFORM_HEADER_SIZE + ch * WS_POINTS_PER_CYCLE * 4 + i * 4;
            if (offset + 4 <= wave_size) {
                memcpy(wave_data + offset, &raw, 4);
            }
        }
    }
    
    /* 计算并记录各通道 RMS 值 */
    {
        const float *channel_ptrs[WS_CHANNEL_COUNT];
        float channel_data[WS_CHANNEL_COUNT][WS_POINTS_PER_CYCLE];
        
        /* 解析各通道数据 */
        for (ch = 0; ch < WS_CHANNEL_COUNT; ch++) {
            for (i = 0; i < WS_POINTS_PER_CYCLE; i++) {
                float v;
                int data_offset = WS_WAVEFORM_HEADER_SIZE + ch * WS_POINTS_PER_CYCLE * 4 + i * 4;
                memcpy(&raw, wave_data + data_offset, 4);
                memcpy(&v, &raw, 4);
                channel_data[ch][i] = v;
            }
        }
        
        /* 计算 RMS */
        float rms[WS_CHANNEL_COUNT];
        for (ch = 0; ch < WS_CHANNEL_COUNT; ch++) {
            rms[ch] = calc_rms(channel_data[ch], WS_POINTS_PER_CYCLE);
        }
        
        LOG_INFO("Waveform RMS values (cycle=%u, scenario=%d):", cycle_seq, g_scenario);
        LOG_INFO("  Ua=%.2fV  Ub=%.2fV  Uc=%.2fV", rms[0], rms[1], rms[2]);
        LOG_INFO("  Ia=%.2fA  Ib=%.2fA  Ic=%.2fA", rms[3], rms[4], rms[5]);
        LOG_INFO("  U0=%.2fV", rms[6]);
        
        /* 计算三相不平衡度 */
        float mean_u = (rms[0] + rms[1] + rms[2]) / 3.0f;
        float max_dev = 0.0f;
        for (ch = 0; ch < 3; ch++) {
            float dev = fabsf(rms[ch] - mean_u);
            if (dev > max_dev) max_dev = dev;
        }
        float unbalance_pct = (mean_u > 0.001f) ? (max_dev / mean_u * 100.0f) : 0.0f;
        LOG_INFO("  三相电压均值=%.2fV, 最大偏差=%.2fV, 不平衡度=%.2f%%", 
                 mean_u, max_dev, unbalance_pct);
    }
}

/* ========== 函数声明 ========== */
static int connect_to_server(const char *ip, int port, int timeout_ms);
static int send_waveform_v2(int sock_fd, uint8_t *wave_data, int wave_len, 
                            uint32_t seq);
static int receive_ai_result_v2(int sock_fd, ai_response_v2_t *result, int timeout_ms);

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
    uint8_t send_buf[WS_SINGLE_WAVEFORM_SIZE * 2 + FRAME_HEADER_SIZE + 4];
    int total_len;
    int sent;
    uint32_t crc;
    
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
    uint8_t temp_buf[WS_SINGLE_WAVEFORM_SIZE * 2];
    memcpy(temp_buf, &header, sizeof(header));
    memcpy(temp_buf + sizeof(header), wave_data, wave_len);
    crc = crc32_calc(temp_buf, sizeof(header) + wave_len);
    
    /* 按照 [header][crc][payload] 格式组装发送缓冲区 */
    memcpy(send_buf, &header, sizeof(header));
    memcpy(send_buf + sizeof(header), &crc, 4);
    memcpy(send_buf + sizeof(header) + 4, wave_data, wave_len);
    
    total_len = sizeof(header) + 4 + wave_len;
    
    LOG_INFO("Sending waveform V2: seq=%u, payload_len=%d, total=%d bytes", 
             seq, wave_len, total_len);
    LOG_DEBUG("Header: magic=0x%08X, ver=%d, cmd=%d, seq=%u, payload_len=%u",
             header.magic, header.version, header.cmd, header.seq, header.payload_len);
    LOG_DEBUG("CRC32: 0x%08X", crc);
    
    sent = send(sock_fd, send_buf, total_len, 0);
    if (sent <= 0) {
        LOG_ERROR("Send failed: sent=%d (errno=%d: %s)", sent, errno, strerror(errno));
        return -1;
    }
    
    LOG_INFO("Sent %d bytes successfully", sent);
    return 0;
}

/* ========== 接收AI推理结果 (V2协议帧) ========== */
static int receive_ai_result_v2(int sock_fd, ai_response_v2_t *result, int timeout_ms)
{
    struct timeval tv;
    fd_set readfds;
    int ret, n;
    uint8_t frame_buf[FRAME_HEADER_SIZE + 4 + AI_RESPONSE_PAYLOAD_SIZE + 256];
    uint8_t *ptr;
    uint8_t *crc_ptr, *payload_ptr;
    uint32_t received_crc, calc_crc;
    uint32_t magic, payload_len;
    uint8_t version, cmd;
    uint32_t seq;
    int retries = 20; /* 最多跳过 20 个中间帧 */
    
    LOG_INFO("Waiting for AI response (V2 frame, timeout: %dms)...", timeout_ms);
    
    while (retries-- > 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        
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
        
        /* 读取 V2 帧头 (14字节) */
        ptr = frame_buf;
        n = recv(sock_fd, ptr, FRAME_HEADER_SIZE, 0);
        if (n < FRAME_HEADER_SIZE) {
            LOG_ERROR("Incomplete V2 header: got %d bytes, expected %d", n, FRAME_HEADER_SIZE);
            return -1;
        }
        
        magic = read_le32(ptr);
        version = ptr[4];
        cmd = ptr[5];
        seq = read_le32(ptr + 6);
        payload_len = read_le32(ptr + 10);
        
        LOG_DEBUG("V2 frame header: magic=0x%08X, ver=%d, cmd=%d, seq=%u, payload_len=%u",
                  magic, version, cmd, seq, payload_len);
        
        if (magic != PROTO_MAGIC) {
            LOG_ERROR("Invalid V2 magic: 0x%08X (expected 0x%08X)", magic, PROTO_MAGIC);
            return -1;
        }
        
        /* 如果是 HEARTBEAT (0x02) 或 ACK (0x03) 帧，跳过并继续等待 */
        if (cmd == 0x02 || cmd == 0x03) {
            LOG_DEBUG("Skipping non-AI frame (cmd=%d), payload_len=%u", cmd, payload_len);
            /* 跳过 CRC32 (4字节) 和负载 */
            {
                uint8_t skip_buf[256];
                int to_skip = 4 + payload_len;
                while (to_skip > 0) {
                    int chunk = (to_skip > (int)sizeof(skip_buf)) ? (int)sizeof(skip_buf) : to_skip;
                    n = recv(sock_fd, skip_buf, chunk, 0);
                    if (n <= 0) {
                        LOG_ERROR("Failed to skip frame data");
                        return -1;
                    }
                    to_skip -= n;
                }
            }
            continue; /* 继续等待下一个帧 */
        }
        
        /* 如果不是 AI_RESULT 帧，报错 */
        if (cmd != CMD_AI_RESULT) {
            LOG_ERROR("Unexpected frame cmd=%d (expected %d)", cmd, CMD_AI_RESULT);
            return -1;
        }
        
        /* 收到 AI_RESULT 帧，继续处理 */
        break;
    }
    
    if (retries <= 0) {
        LOG_ERROR("Failed to get AI result after retries");
        return -1;
    }
    
    /* 验证帧头中的 payload_len */
    if (payload_len != AI_RESPONSE_PAYLOAD_SIZE) {
        LOG_ERROR("Unexpected payload length: %u (expected %d)", payload_len, AI_RESPONSE_PAYLOAD_SIZE);
        return -1;
    }
    
    /* 读取 CRC32 (4字节) */
    crc_ptr = frame_buf + FRAME_HEADER_SIZE;
    n = recv(sock_fd, crc_ptr, 4, 0);
    if (n < 4) {
        LOG_ERROR("Incomplete CRC32: got %d bytes, expected 4", n);
        return -1;
    }
    
    /* 读取负载 (AI响应数据) */
    payload_ptr = frame_buf + FRAME_HEADER_SIZE + 4;
    n = recv(sock_fd, payload_ptr, payload_len, 0);
    if (n < (int)payload_len) {
        LOG_ERROR("Incomplete AI payload: got %d bytes, expected %u", n, payload_len);
        return -1;
    }
    
    /* 验证 V2 帧 CRC32 */
    received_crc = read_le32(crc_ptr);
    
    /* 
     * CRC32 验证: 计算 header + payload 的 CRC32
     * frame_buf 结构: [header(14)][crc(4)][payload]
     * 需要计算 header + payload 的 CRC32，排除 CRC32 本身
     */
    {
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
    
    /* 从负载解析 AI 响应结构 (逐字段解析, 确保字节序兼容性) */
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

/* ========== 主函数 ========== */
int main(int argc, char *argv[])
{
    int max_cycles = 5;
    int interval_ms = 500;
    const char *server_ip = DEFAULT_SERVER_IP;
    int server_port = DEFAULT_SERVER_PORT;
    const char *log_file = "wave_sender_test.log";
    
    int i, ret;
    uint8_t *wave_buf = NULL;
    uint32_t send_seq = 0;

    crc32_init_table();
    srand(time(NULL));

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
        } else if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            g_scenario = atoi(argv[++i]);
            if (g_scenario < 0 || g_scenario > 4) {
                LOG_WARN("Invalid scenario %d, using default (1=SINGLE_OPEN)", g_scenario);
                g_scenario = 1;
            }
        } else if (strcmp(argv[i], "--debug") == 0) {
            g_log_level = 3;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: wave_sender_test [options]\n");
            printf("Options:\n");
            printf("  --cycles N       Number of cycles (default: 5)\n");
            printf("  --interval MS    Interval ms (default: 500)\n");
            printf("  --server IP      RK3576 server IP (default: 192.168.100.1)\n");
            printf("  --port PORT      RK3576 port (default: 9090)\n");
            printf("  --scenario N     Test scenario (default: 1)\n");
            printf("    0: Normal three-phase balance\n");
            printf("    1: Single-phase open (A normal, B/C open) [DEFAULT]\n");
            printf("    2: Voltage sag (all phases to 140V)\n");
            printf("    3: Harmonic distortion (3rd/5th/7th harmonics)\n");
            printf("    4: Three-phase unbalance\n");
            printf("  --log FILE       Log file (default: wave_sender_test.log)\n");
            printf("  --debug          Enable debug level logging\n");
            return 0;
        }
    }

    g_log_fp = fopen(log_file, "w");
    if (g_log_fp) {
        setvbuf(g_log_fp, NULL, _IOLBF, 0);
    }
    
    LOG_INFO("============================================================");
    LOG_INFO("T536 Wave Collector & AI Sender (V2 Protocol Test)");
    LOG_INFO("Protocol: WV2 (magic=0x%08X)", PROTO_MAGIC);
    LOG_INFO("============================================================");
    LOG_INFO("Configuration:");
    LOG_INFO("  Cycles: %d", max_cycles);
    LOG_INFO("  Interval: %dms", interval_ms);
    LOG_INFO("  Server: %s:%d", server_ip, server_port);
    LOG_INFO("  Log file: %s", log_file);
    LOG_INFO("  Scenario: %d", g_scenario);
    switch (g_scenario) {
        case 0: LOG_INFO("    0: Normal three-phase balance (220V RMS)"); break;
        case 1: LOG_INFO("    1: Single-phase open (A=220V, B/C≈0V)"); break;
        case 2: LOG_INFO("    2: Voltage sag (all phases to 140V RMS)"); break;
        case 3: LOG_INFO("    3: Harmonic distortion (3rd/5th/7th harmonics)"); break;
        case 4: LOG_INFO("    4: Three-phase unbalance (B=187V, C=253V)"); break;
    }
    LOG_INFO("");

    wave_buf = (uint8_t *)malloc(WS_SINGLE_WAVEFORM_SIZE);
    if (wave_buf == NULL) {
        LOG_ERROR("Memory allocation failed");
        if (g_log_fp) fclose(g_log_fp);
        return 1;
    }
    LOG_INFO("Buffer allocated: %d bytes", WS_SINGLE_WAVEFORM_SIZE);

    int success_count = 0;
    int fail_count = 0;
    
    LOG_INFO("Starting main loop...");
    LOG_INFO("");
    
    for (i = 0; i < max_cycles; i++) {
        LOG_INFO("------------------------------------------------------------");
        LOG_INFO("[TEST] Cycle %d/%d", i + 1, max_cycles);
        LOG_INFO("------------------------------------------------------------");

        /* 1. 生成模拟波形 */
        LOG_INFO("[STEP 1] Generating test waveform...");
        generate_test_waveform(wave_buf, WS_SINGLE_WAVEFORM_SIZE, i + 1);
        LOG_INFO("Waveform generated: %d bytes", WS_SINGLE_WAVEFORM_SIZE);

        /* 2. 连接RK3576并发送 (V2协议) */
        LOG_INFO("[STEP 2] Connecting to RK3576 and sending waveform (V2)...");
        
        int sock_fd = connect_to_server(server_ip, server_port, DEFAULT_TIMEOUT_MS);
        if (sock_fd < 0) {
            LOG_ERROR("Cannot connect to RK3576, skipping this cycle");
            fail_count++;
            usleep(interval_ms * 1000);
            continue;
        }
        
        send_seq++;
        
        int send_ret = send_waveform_v2(sock_fd, wave_buf, WS_SINGLE_WAVEFORM_SIZE, send_seq);
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
                
                switch (ai_result.cnn_class) {
                    case 0: LOG_INFO("  Interpretation: Normal operation"); break;
                    case 1: LOG_INFO("  Interpretation: Voltage sag"); break;
                    case 2: LOG_INFO("  Interpretation: Voltage swell"); break;
                    case 3: LOG_INFO("  Interpretation: Harmonic distortion"); break;
                    case 4: LOG_INFO("  Interpretation: Three-phase unbalance"); break;
                    case 5: LOG_INFO("  Interpretation: Overload"); break;
                    case 6: LOG_INFO("  Interpretation: Transient pulse"); break;
                    default: LOG_INFO("  Interpretation: Unknown (class=%d)", ai_result.cnn_class);
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
        
        if (i < max_cycles - 1) {
            LOG_DEBUG("Waiting %dms before next cycle...", interval_ms);
            usleep(interval_ms * 1000);
        }
    }

    LOG_INFO("============================================================");
    LOG_INFO("Test completed (V2 Protocol)!");
    LOG_INFO("  Success: %d", success_count);
    LOG_INFO("  Failed: %d", fail_count);
    LOG_INFO("  Total: %d", max_cycles);
    LOG_INFO("  Success rate: %.1f%%", (max_cycles > 0) ? (success_count * 100.0 / max_cycles) : 0);
    LOG_INFO("  Log file: %s", log_file);
    LOG_INFO("============================================================");

    free(wave_buf);
    
    if (g_log_fp) {
        fclose(g_log_fp);
        g_log_fp = NULL;
    }

    return (fail_count == 0) ? 0 : 1;
}