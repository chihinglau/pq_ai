/**
 * @file ai_rpc_logger_arm.c
 * @brief AI 推理 RPC 日志记录器 - 带完整日志文件记录
 * 
 * 功能：
 * 1. 从T536硬件读取实时波形数据
 * 2. 提取27维特征向量
 * 3. 尝试通过USB ECM发送特征到RK3576算力卡
 * 4. 所有请求/响应记录到日志文件
 * 5. 支持实时查看日志
 * 
 * 用法: ai_rpc_logger_arm [options]
 *   --cycles N     测试周期数（默认5）
 *   --interval MS  采集间隔毫秒（默认500）
 *   --log FILE     日志文件名（默认ai_rpc.log）
 *   --rpc          启用真实RPC发送到算力卡
 *   --help         显示帮助
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
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
#define PQ_POINTS_PER_CYCLE_25600 512
#define PQ_N_CHANNELS 7
#define N_TOTAL_FEATURES 27

/* USB ECM 配置 - T536与RK3576直连 */
#define USB_ECM_HOST_IP     "192.168.100.1"   /* RK3576 usb0 */
#define USB_ECM_PORT         9090
#define USB_ECM_TIMEOUT_MS   2000

/* ========== 特征向量结构 ========== */
typedef struct {
    float features[N_TOTAL_FEATURES];
    char  names[N_TOTAL_FEATURES][32];
    uint16_t n_features;
} feature_vector_t;

/* ========== 周波数据结构 ========== */
typedef struct {
    float samples[PQ_N_CHANNELS][PQ_POINTS_PER_CYCLE_25600];
    uint32_t cycle_id;
    uint64_t timestamp_us;
} wave_cycle_t;

/* ========== PQ指标结构（简化版） ========== */
typedef struct {
    float value;
} pq_value_t;

typedef struct {
    pq_value_t voltage_deviation;
    pq_value_t line_loss;
    pq_value_t transformer_load;
    pq_value_t line_load;
    pq_value_t voltage_thd;
    pq_value_t current_thd;
    pq_value_t voltage_unbalance;
    pq_value_t frequency_deviation;
    pq_value_t active_power;
    pq_value_t reactive_power;
    pq_value_t power_factor;
    pq_value_t harmonic_3rd;
} pq_metrics_t;

/* ========== AI结果结构 ========== */
typedef struct {
    float if_score;
    float ae_score;
    int   cnn_class;
    float cnn_confidence;
    int   latency_ms;
    int   module_available;
    char  raw_response[512];
} ai_result_t;

/* ========== 全局日志文件 ========== */
static FILE *g_log_fp = NULL;

/* ========== 日志宏 ========== */
#define LOG(fmt, ...) do { \
    time_t now = time(NULL); \
    char time_str[64]; \
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now)); \
    printf("[%s] " fmt "\n", time_str, ##__VA_ARGS__); \
    if (g_log_fp) { \
        fprintf(g_log_fp, "[%s] " fmt "\n", time_str, ##__VA_ARGS__); \
        fflush(g_log_fp); \
    } \
} while(0)

/* ========== 函数声明 ========== */
static uint32 read_le32(const uint8_t *p);
static int parse_wave_frame(uint8_t *data, int32 data_len, float *channels[7], int *frame_seq);
static float calc_rms(const float *x, int n);
static int feature_extract_from_wave(const wave_cycle_t *cycles, int n_cycles,
                                     const pq_metrics_t *metrics, feature_vector_t *out);
static int ai_rpc_send(const feature_vector_t *feat, const pq_metrics_t *metrics, ai_result_t *result);
static int build_json_request(char *buf, int buf_size, const feature_vector_t *feat, const pq_metrics_t *metrics);

/* ========== 字节序读取 ========== */
static uint32 read_le32(const uint8_t *p)
{
    return ((uint32)p[0]) |
           ((uint32)p[1] << 8) |
           ((uint32)p[2] << 16) |
           ((uint32)p[3] << 24);
}

/* ========== RMS计算 ========== */
static float calc_rms(const float *x, int n)
{
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        sum += x[i] * x[i];
    }
    return sqrtf(sum / (float)n);
}

/* ========== 波形帧解析 ========== */
static int parse_wave_frame(uint8_t *data, int32 data_len, float *channels[7], int *frame_seq)
{
    int idx = 0;
    int frame_count = 0;
    
    while (idx < data_len - 1) {
        if (data[idx] == 0x68 && data[idx + 1] == 0x36) {
            if (idx + WS_SINGLE_WAVEFORM_SIZE + 7 > data_len) break;
            
            int wave_start = idx + 7;
            *frame_seq = read_le32(&data[wave_start]);
            
            int data_offset = wave_start + 14;
            
            for (int ch = 0; ch < WS_CHANNEL_COUNT; ch++) {
                for (int pt = 0; pt < WS_POINTS_PER_CYCLE; pt++) {
                    uint32_t raw_val = read_le32(&data[data_offset]);
                    float float_val;
                    memcpy(&float_val, &raw_val, sizeof(float));
                    channels[ch][pt] = float_val;
                    data_offset += 4;
                }
            }
            
            frame_count++;
            idx += WS_SINGLE_WAVEFORM_SIZE + 7;
        } else {
            idx++;
        }
    }
    
    return frame_count;
}

/* ========== 构建JSON请求 ========== */
static int build_json_request(char *buf, int buf_size, 
                               const feature_vector_t *feat, 
                               const pq_metrics_t *metrics)
{
    int i, offset;
    
    offset = snprintf(buf, (size_t)buf_size,
        "{\"cmd\":\"infer\",\"features\":[");
    
    for (i = 0; i < feat->n_features && offset < buf_size - 50; i++) {
        offset += snprintf(buf + offset, (size_t)(buf_size - offset),
                           "%.4f%s", feat->features[i],
                           (i < feat->n_features - 1) ? "," : "");
    }
    
    offset += snprintf(buf + offset, (size_t)(buf_size - offset),
        "],\"vthd\":%.3f,\"ithd\":%.3f,\"ts\":%u}",
        metrics->voltage_thd.value,
        metrics->current_thd.value,
        (unsigned int)time(NULL));
    
    return (offset > 0 && offset < buf_size) ? 0 : -1;
}

/* ========== 解析JSON响应 ========== */
static int parse_json_response(const char *resp, ai_result_t *result)
{
    const char *p;
    
    memset(result, 0, sizeof(*result));
    strncpy(result->raw_response, resp, sizeof(result->raw_response) - 1);
    
    /* 解析 if_score */
    p = strstr(resp, "\"if\":");
    if (p) {
        p += 4;
        result->if_score = strtof(p, NULL);
    }
    
    /* 解析 ae_score */
    p = strstr(resp, "\"ae\":");
    if (p) {
        p += 4;
        result->ae_score = strtof(p, NULL);
    }
    
    /* 解析 cnn_class */
    p = strstr(resp, "\"cls\":");
    if (p) {
        p += 5;
        result->cnn_class = (int)strtol(p, NULL, 10);
    }
    
    /* 解析 cnn_confidence */
    p = strstr(resp, "\"conf\":");
    if (p) {
        p += 7;
        result->cnn_confidence = strtof(p, NULL);
    }
    
    /* 解析 latency_ms */
    p = strstr(resp, "\"lat\":");
    if (p) {
        p += 5;
        result->latency_ms = (int)strtol(p, NULL, 10);
    }
    
    return 0;
}

/* ========== USB ECM 发送请求 ========== */
static int usb_ecm_send_request(const char *request, char *response, int resp_size, int timeout_ms)
{
    int sock_fd;
    struct sockaddr_in serv_addr;
    fd_set readfds;
    struct timeval tv;
    int ret;
    
    /* 创建socket */
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        LOG("[USB_ECM] socket创建失败");
        return -1;
    }
    
    /* 设置超时 */
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    /* 连接RK3576 */
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(USB_ECM_PORT);
    inet_pton(AF_INET, USB_ECM_HOST_IP, &serv_addr.sin_addr);
    
    LOG("[USB_ECM] 连接 %s:%d...", USB_ECM_HOST_IP, USB_ECM_PORT);
    
    ret = connect(sock_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    if (ret < 0) {
        LOG("[USB_ECM] 连接失败 (可能RK3576未启动)");
        close(sock_fd);
        return -1;
    }
    
    LOG("[USB_ECM] 连接成功");
    
    /* 发送请求 */
    ret = send(sock_fd, request, strlen(request), 0);
    if (ret <= 0) {
        LOG("[USB_ECM] 发送失败");
        close(sock_fd);
        return -1;
    }
    LOG("[USB_ECM] 发送 %d 字节", ret);
    
    /* 接收响应 */
    FD_ZERO(&readfds);
    FD_SET(sock_fd, &readfds);
    
    ret = select(sock_fd + 1, &readfds, NULL, NULL, &tv);
    if (ret <= 0) {
        LOG("[USB_ECM] 等待响应超时");
        close(sock_fd);
        return -1;
    }
    
    ret = recv(sock_fd, response, resp_size - 1, 0);
    if (ret <= 0) {
        LOG("[USB_ECM] 接收失败");
        close(sock_fd);
        return -1;
    }
    response[ret] = '\0';
    LOG("[USB_ECM] 接收 %d 字节", ret);
    
    close(sock_fd);
    return 0;
}

/* ========== AI RPC推理 ========== */
static int ai_rpc_send(const feature_vector_t *feat, 
                        const pq_metrics_t *metrics, 
                        ai_result_t *result)
{
    char request[2048];
    char response[512];
    int ret;
    struct timeval tv_start, tv_end;
    int latency_ms;
    
    memset(result, 0, sizeof(*result));
    
    /* 构建JSON请求 */
    if (build_json_request(request, sizeof(request), feat, metrics) != 0) {
        LOG("[AI_RPC] 构建请求失败");
        return -1;
    }
    
    LOG("[AI_RPC] ========== 发送请求到RK3576 ==========");
    LOG("[AI_RPC] 请求长度: %zu 字节", strlen(request));
    LOG("[AI_RPC] 请求内容: %s", request);
    
    gettimeofday(&tv_start, NULL);
    
    /* 尝试通过USB ECM发送 */
    ret = usb_ecm_send_request(request, response, sizeof(response), USB_ECM_TIMEOUT_MS);
    
    gettimeofday(&tv_end, NULL);
    latency_ms = (tv_end.tv_sec - tv_start.tv_sec) * 1000 + 
                 (tv_end.tv_usec - tv_start.tv_usec) / 1000;
    
    if (ret == 0) {
        /* 成功收到响应 */
        result->module_available = 1;
        result->latency_ms = latency_ms;
        
        LOG("[AI_RPC] ========== 收到RK3576响应 ==========");
        LOG("[AI_RPC] 响应内容: %s", response);
        LOG("[AI_RPC] 延迟: %dms", latency_ms);
        
        /* 解析响应 */
        parse_json_response(response, result);
        
        LOG("[AI_RPC] ========== 解析结果 ==========");
        LOG("[AI_RPC] iForest得分: %.4f", result->if_score);
        LOG("[AI_RPC] AE重构误差: %.4f", result->ae_score);
        LOG("[AI_RPC] CNN分类: %d (置信度: %.2f)", result->cnn_class, result->cnn_confidence);
        
    } else {
        /* RK3576不可达，本地模拟推理 */
        result->module_available = 0;
        result->latency_ms = latency_ms;
        
        LOG("[AI_RPC] ========== RK3576不可达，本地模拟推理 ==========");
        LOG("[AI_RPC] 延迟: %dms", latency_ms);
        
        /* 本地iForest模拟 */
        {
            float ua = feat->features[0];
            float ub = feat->features[1];
            float uc = feat->features[2];
            float mean_u = (ua + ub + uc) / 3.0f;
            float variance = 0;
            int i;
            
            for (i = 0; i < 3; i++) {
                float v = feat->features[i];
                variance += (v - mean_u) * (v - mean_u);
            }
            float std_u = sqrtf(variance / 3.0f);
            float cv = (mean_u > 1e-6f) ? (std_u / mean_u) : 0;
            
            result->if_score = (cv > 1.0f) ? 1.0f : cv;
            result->ae_score = (result->if_score > 0.5f) ? 0.8f : 0.1f;
            
            /* 本地CNN分类 */
            if (result->if_score > 0.5f) {
                result->cnn_class = 3;
                result->cnn_confidence = 0.90f;
            } else if (result->if_score > 0.2f) {
                result->cnn_class = 2;
                result->cnn_confidence = 0.70f;
            } else {
                result->cnn_class = 0;
                result->cnn_confidence = 0.80f;
            }
            
            snprintf(result->raw_response, sizeof(result->raw_response),
                     "{\"if\":%.4f,\"ae\":%.4f,\"cls\":%d,\"conf\":%.2f,\"lat\":%d,\"sim\":true}",
                     result->if_score, result->ae_score, result->cnn_class, 
                     result->cnn_confidence, result->latency_ms);
        }
        
        LOG("[AI_RPC] ========== 模拟推理结果 ==========");
        LOG("[AI_RPC] 响应内容: %s", result->raw_response);
        LOG("[AI_RPC] iForest得分: %.4f", result->if_score);
        LOG("[AI_RPC] AE重构误差: %.4f", result->ae_score);
        LOG("[AI_RPC] CNN分类: %d (置信度: %.2f)", result->cnn_class, result->cnn_confidence);
    }
    
    LOG("[AI_RPC] 算力卡状态: %s", result->module_available ? "✅ 在线" : "❌ 离线(本地模拟)");
    
    return 0;
}

/* ========== 特征提取 ========== */
static int feature_extract_from_wave(const wave_cycle_t *cycles, int n_cycles,
                                     const pq_metrics_t *metrics, feature_vector_t *out)
{
    int i, ch;
    float rms_ua = 0, rms_ub = 0, rms_uc = 0;
    float rms_ia = 0, rms_ib = 0, rms_ic = 0;
    int pts = 256;

    if (out == NULL || metrics == NULL) return -1;
    memset(out, 0, sizeof(feature_vector_t));

    /* 从波形数据直接计算各相RMS */
    if (cycles != NULL && n_cycles > 0) {
        const wave_cycle_t *cycle = &cycles[0];

        rms_ua = calc_rms(cycle->samples[0], pts);
        rms_ub = calc_rms(cycle->samples[1], pts);
        rms_uc = calc_rms(cycle->samples[2], pts);
        rms_ia = calc_rms(cycle->samples[3], pts);
        rms_ib = calc_rms(cycle->samples[4], pts);
        rms_ic = calc_rms(cycle->samples[5], pts);
    }

    /* 标准特征 0-15 */
    out->features[0] = rms_ua;   strncpy(out->names[0], "voltage_rms_a", 31);
    out->features[1] = rms_ub;   strncpy(out->names[1], "voltage_rms_b", 31);
    out->features[2] = rms_uc;   strncpy(out->names[2], "voltage_rms_c", 31);
    out->features[3] = rms_ia;   strncpy(out->names[3], "current_rms_a", 31);
    out->features[4] = rms_ib;   strncpy(out->names[4], "current_rms_b", 31);
    out->features[5] = rms_ic;   strncpy(out->names[5], "current_rms_c", 31);
    out->features[6] = metrics->active_power.value;
    out->features[7] = metrics->reactive_power.value;
    out->features[8] = metrics->power_factor.value;
    out->features[9] = metrics->voltage_thd.value;
    out->features[10] = metrics->current_thd.value;
    out->features[11] = metrics->voltage_unbalance.value;
    out->features[12] = metrics->frequency_deviation.value;
    out->features[13] = metrics->transformer_load.value;
    out->features[14] = metrics->line_load.value;
    out->features[15] = metrics->harmonic_3rd.value;

    /* 波形特征 16-26：三相平均 */
    if (cycles != NULL && n_cycles > 0) {
        const wave_cycle_t *cycle = &cycles[0];
        float cf_sum = 0, area_sum = 0, pp_sum = 0, zc_sum = 0;
        float slope_sum = 0, std_sum = 0;

        for (ch = 0; ch < 3; ch++) {
            const float *v = cycle->samples[ch];
            float rms = calc_rms(v, pts);
            float peak = 0;
            float max_v = v[0], min_v = v[0];
            float mean = 0;
            int zc = 0;
            int j;

            for (j = 0; j < pts; j++) {
                if (fabsf(v[j]) > peak) peak = fabsf(v[j]);
                if (v[j] > max_v) max_v = v[j];
                if (v[j] < min_v) min_v = v[j];
                mean += v[j];
            }
            mean /= pts;

            cf_sum += (rms > 1e-6f) ? (peak / rms) : 0;
            pp_sum += (max_v - min_v);
            area_sum += peak;

            for (j = 1; j < pts; j++) {
                if ((v[j-1] > 0 && v[j] <= 0) || (v[j-1] < 0 && v[j] >= 0)) zc++;
                slope_sum += fabsf(v[j] - v[j-1]);
            }
            zc_sum += zc;
            slope_sum /= (pts - 1);

            float std = 0;
            for (j = 0; j < pts; j++) {
                float d = v[j] - mean;
                std += d * d;
            }
            std_sum += sqrtf(std / pts);
        }

        out->features[16] = cf_sum / 3.0f;  strncpy(out->names[16], "crest_factor", 31);
        out->features[17] = area_sum / 3.0f; strncpy(out->names[17], "wave_area", 31);
        out->features[18] = pp_sum / 3.0f;  strncpy(out->names[18], "peak_peak", 31);
        out->features[19] = zc_sum / 3.0f;  strncpy(out->names[19], "zero_crossings", 31);
        out->features[20] = slope_sum / 3.0f; strncpy(out->names[20], "slope_mean", 31);
        out->features[21] = std_sum / 3.0f;  strncpy(out->names[21], "std_dev", 31);
        out->features[22] = 0;                strncpy(out->names[22], "slope_std", 31);
        out->features[23] = 0;                strncpy(out->names[23], "half_wave_asym", 31);
        out->features[24] = 0;                strncpy(out->names[24], "transient_energy", 31);
        out->features[25] = 0;                strncpy(out->names[25], "approx_entropy", 31);
        out->features[26] = 0;                strncpy(out->names[26], "lle_approx", 31);
    }

    out->n_features = N_TOTAL_FEATURES;
    return 0;
}

/* ========== 主函数 ========== */
int main(int argc, char *argv[])
{
    int max_cycles = 5;
    int interval_ms = 500;
    const char *log_file = "ai_rpc.log";
    int enable_rpc = 0;
    int i, ret;
    WAVEFORM_SAMPLER_DEVICE_T *wave_dev = NULL;
    HW_DEVICE *dev = NULL;
    uint8_t *wave_buf = NULL;
    int32_t wave_buf_size = WS_SINGLE_WAVEFORM_SIZE * 20;

    /* 解析命令行参数 */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--cycles") == 0 && i + 1 < argc) {
            max_cycles = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
            interval_ms = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            log_file = argv[++i];
        } else if (strcmp(argv[i], "--rpc") == 0) {
            enable_rpc = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: ai_rpc_logger_arm [options]\n");
            printf("Options:\n");
            printf("  --cycles N       测试周期数 (default: 5)\n");
            printf("  --interval MS    采集间隔毫秒 (default: 500)\n");
            printf("  --log FILE       日志文件名 (default: ai_rpc.log)\n");
            printf("  --rpc            启用真实RPC发送到算力卡\n");
            printf("  --help           Show this help\n");
            return 0;
        }
    }

    /* 打开日志文件 */
    g_log_fp = fopen(log_file, "a");
    if (g_log_fp == NULL) {
        printf("[WARN] 无法打开日志文件 %s，仅输出到终端\n", log_file);
    } else {
        LOG("日志文件已打开: %s", log_file);
    }

    LOG("============================================================");
    LOG("  T536 AI RPC 日志记录器");
    LOG("  功能: 波形采集 → 特征提取 → RK3576推理 → 日志记录");
    LOG("============================================================");
    LOG("配置: cycles=%d, interval=%dms, rpc=%s", 
        max_cycles, interval_ms, enable_rpc ? "启用" : "禁用");
    LOG("");

    /* 初始化HAL */
    LOG("初始化HAL...");
    ret = hal_init();
    if (ret != 0) {
        LOG("[ERROR] HAL初始化失败: %ld", ret);
        if (g_log_fp) fclose(g_log_fp);
        return 1;
    }
    LOG("HAL初始化成功");

    dev = hal_device_get(WAVEFORM_SAMPLER_HARDWARE_MODULE_ID);
    if (dev == NULL) {
        LOG("[ERROR] 获取波形采样设备失败");
        hal_exit();
        if (g_log_fp) fclose(g_log_fp);
        return 1;
    }
    wave_dev = (WAVEFORM_SAMPLER_DEVICE_T *)dev;
    LOG("设备获取成功 (pModule=%p)", wave_dev->base.pModule);

    if (wave_dev->read_waveform == NULL) {
        LOG("[ERROR] read_waveform函数指针为空");
        hal_device_release(dev);
        hal_exit();
        if (g_log_fp) fclose(g_log_fp);
        return 1;
    }

    wave_buf = (uint8_t *)malloc(wave_buf_size);
    if (wave_buf == NULL) {
        LOG("[ERROR] 内存分配失败");
        hal_device_release(dev);
        hal_exit();
        if (g_log_fp) fclose(g_log_fp);
        return 1;
    }

    /* 主测试循环 */
    for (i = 0; i < max_cycles; i++) {
        LOG("------------------------------------------------------------");
        LOG("[TEST] 周期 %d/%d", i + 1, max_cycles);
        LOG("------------------------------------------------------------");

        /* 1. 读取波形 */
        memset(wave_buf, 0, wave_buf_size);
        ret = wave_dev->read_waveform(wave_dev, wave_buf, wave_buf_size, 1);
        
        if (ret <= 0) {
            LOG("[WARN] 读取波形失败 (ret=%ld)，重试...", ret);
            usleep(100000);
            i--;
            continue;
        }
        LOG("[WAVE] 读取 %ld 字节波形数据", ret);

        /* 2. 解析波形 */
        float *channels[7];
        int frame_seq = 0;
        int ch, pt;
        wave_cycle_t cycle;

        for (ch = 0; ch < 7; ch++) {
            channels[ch] = (float *)malloc(256 * sizeof(float));
            memset(channels[ch], 0, 256 * sizeof(float));
        }

        int frame_count = parse_wave_frame(wave_buf, ret, channels, &frame_seq);
        LOG("[WAVE] 解析到 %d 个帧 (seq=%d)", frame_count, frame_seq);

        if (frame_count > 0) {
            memset(&cycle, 0, sizeof(cycle));
            cycle.cycle_id = frame_seq;
            for (ch = 0; ch < 7; ch++) {
                for (pt = 0; pt < 256; pt++) {
                    cycle.samples[ch][pt] = channels[ch][pt];
                }
            }

            /* 打印统计 */
            const char *ch_names[7] = {"UA", "UB", "UC", "IA", "IB", "IC", "IZ"};
            LOG("[WAVE] 通道统计:");
            for (ch = 0; ch < 7; ch++) {
                float min_v = channels[ch][0], max_v = channels[ch][0];
                float sum_sq = 0;
                for (pt = 0; pt < 256; pt++) {
                    if (channels[ch][pt] < min_v) min_v = channels[ch][pt];
                    if (channels[ch][pt] > max_v) max_v = channels[ch][pt];
                    sum_sq += channels[ch][pt] * channels[ch][pt];
                }
                float rms = sqrtf(sum_sq / 256.0f);
                LOG("  %s: min=%10.3f max=%10.3f rms=%10.3f",
                    ch_names[ch], min_v, max_v, rms);
            }

            /* 3. 特征提取 */
            feature_vector_t feat;
            pq_metrics_t metrics;
            ai_result_t ai_result;

            memset(&metrics, 0, sizeof(metrics));
            feature_extract_from_wave(&cycle, 1, &metrics, &feat);

            LOG("[FEAT] 27维特征向量:");
            for (int f = 0; f < N_TOTAL_FEATURES; f++) {
                LOG("  [%2d] %-20s = %10.4f", f, feat.names[f], feat.features[f]);
            }

            /* 4. AI推理 */
            if (enable_rpc) {
                ai_rpc_send(&feat, &metrics, &ai_result);
            } else {
                /* 本地模拟 */
                LOG("[AI_RPC] --rpc未启用，使用本地模拟--");
                ai_rpc_send(&feat, &metrics, &ai_result);
            }

            /* 5. 结论 */
            LOG("[RESULT] ========== 推理结论 ==========");
            if (ai_result.if_score > 0.5f) {
                LOG("[RESULT] ⚠️ 检测到异常工况！");
                LOG("[RESULT]   类型: CNN class=%d", ai_result.cnn_class);
                if (ai_result.cnn_class == 3) {
                    LOG("[RESULT]   诊断: 单相开路/接地故障");
                    LOG("[RESULT]   特征: B/C相电压远低于A相");
                }
                LOG("[RESULT]   置信度: %.2f", ai_result.cnn_confidence);
                LOG("[RESULT]   建议: 检查B/C相线路连接");
            } else {
                LOG("[RESULT] ✅ 波形基本正常");
            }
            LOG("[RESULT] 算力卡: %s | 延迟: %dms",
                ai_result.module_available ? "在线" : "离线",
                ai_result.latency_ms);
        }

        /* 释放内存 */
        for (ch = 0; ch < 7; ch++) free(channels[ch]);

        LOG("");
        usleep(interval_ms * 1000);
    }

    /* 清理 */
    LOG("============================================================");
    LOG("测试完成，共执行 %d 个周期", max_cycles);
    LOG("日志文件: %s", log_file);
    LOG("查看日志: tail -f %s", log_file);
    LOG("============================================================");

    free(wave_buf);
    hal_device_release(dev);
    hal_exit();
    if (g_log_fp) fclose(g_log_fp);

    return 0;
}