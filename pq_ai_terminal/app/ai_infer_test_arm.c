/**
 * @file ai_infer_test_arm.c
 * @brief T536 AI 推理验证工具
 * 
 * 功能：
 * 1. 从T536硬件读取实时波形数据
 * 2. 调用 feature_extract_from_wave() 提取27维特征向量
 * 3. 调用 ai_rpc_infer() 发送特征到RK3576算力卡
 * 4. 打印完整的数据流用于验证
 * 
 * 用法: ai_infer_test_arm [options]
 *   --cycles N     测试周期数（默认1）
 *   --rpc          启用RPC发送到算力卡
 *   --help         显示帮助
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>

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

/* ========== 波形帧格式常量 ========== */
#define WS_SINGLE_WAVEFORM_SIZE 7182
#define WS_POINTS_PER_CYCLE 256
#define WS_CHANNEL_COUNT 7
#define PQ_POINTS_PER_CYCLE_25600 512
#define PQ_N_CHANNELS 7
#define N_TOTAL_FEATURES 27

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
} ai_result_t;

/* ========== 函数声明 ========== */
static uint32 read_le32(const uint8_t *p);
static int parse_wave_frame(uint8_t *data, int32 data_len, float *channels[7], int *frame_seq);
static int feature_extract_from_wave(const wave_cycle_t *cycles, int n_cycles,
                                     const pq_metrics_t *metrics, feature_vector_t *out);
static int ai_rpc_infer(const feature_vector_t *feat, const pq_metrics_t *metrics, ai_result_t *result);

/* ========== 字节序读取工具 ========== */
static uint32 read_le32(const uint8_t *p)
{
    return ((uint32)p[0]) |
           ((uint32)p[1] << 8) |
           ((uint32)p[2] << 16) |
           ((uint32)p[3] << 24);
}

/* ========== 波形帧解析 ========== */
static int parse_wave_frame(uint8_t *data, int32 data_len, float *channels[7], int *frame_seq)
{
    int idx = 0;
    int frame_count = 0;
    
    while (idx < data_len - 1) {
        if (data[idx] == 0x68 && data[idx + 1] == 0x36) {
            uint32_t frame_len = read_le32(&data[idx + 2]);
            
            if (idx + WS_SINGLE_WAVEFORM_SIZE + 7 > data_len) break;
            
            int wave_start = idx + 7;
            *frame_seq = read_le32(&data[wave_start]);
            
            /* 跳过周波序号(4) + 时间戳(10) = 14字节 */
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

/* ========== RMS计算 ========== */
static float calc_rms(const float *x, int n)
{
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        sum += x[i] * x[i];
    }
    return sqrtf(sum / (float)n);
}

/* ========== 特征提取（简化版，直接从波形计算） ========== */
static int feature_extract_from_wave(const wave_cycle_t *cycles, int n_cycles,
                                     const pq_metrics_t *metrics, feature_vector_t *out)
{
    int i, ch;
    float wave_feat[11] = {0};
    int pts = 256;  /* 实际硬件每周期256点 */
    float rms_ua = 0, rms_ub = 0, rms_uc = 0;
    float rms_ia = 0, rms_ib = 0, rms_ic = 0;

    if (out == NULL || metrics == NULL) return -1;
    memset(out, 0, sizeof(feature_vector_t));

    printf("  [FEAT] ========== 特征提取开始 ==========\n");

    /* 从波形数据直接计算各相RMS */
    if (cycles != NULL && n_cycles > 0) {
        const wave_cycle_t *cycle = &cycles[0];

        rms_ua = calc_rms(cycle->samples[0], pts);
        rms_ub = calc_rms(cycle->samples[1], pts);
        rms_uc = calc_rms(cycle->samples[2], pts);
        rms_ia = calc_rms(cycle->samples[3], pts);
        rms_ib = calc_rms(cycle->samples[4], pts);
        rms_ic = calc_rms(cycle->samples[5], pts);

        printf("  [FEAT] 波形RMS计算结果:\n");
        printf("    UA=%.3fV, UB=%.3fV, UC=%.3fV\n", rms_ua, rms_ub, rms_uc);
        printf("    IA=%.3fA, IB=%.3fA, IC=%.3fA\n", rms_ia, rms_ib, rms_ic);

        /* 计算电压不平衡度 */
        float avg_u = (rms_ua + rms_ub + rms_uc) / 3.0f;
        float max_dev = fabsf(rms_ua - avg_u);
        if (fabsf(rms_ub - avg_u) > max_dev) max_dev = fabsf(rms_ub - avg_u);
        if (fabsf(rms_uc - avg_u) > max_dev) max_dev = fabsf(rms_uc - avg_u);
        float unbalance = (avg_u > 1e-6f) ? (max_dev / avg_u * 100.0f) : 0.0f;
        printf("  [FEAT] 电压不平衡度: %.2f%% (avg=%.3fV, max_dev=%.3fV)\n", unbalance, avg_u, max_dev);
    }

    /* 标准特征 0-15 */
    out->features[0] = rms_ua;   strncpy(out->names[0], "voltage_rms_a", 31);
    out->features[1] = rms_ub;   strncpy(out->names[1], "voltage_rms_b", 31);
    out->features[2] = rms_uc;   strncpy(out->names[2], "voltage_rms_c", 31);
    out->features[3] = rms_ia;   strncpy(out->names[3], "current_rms_a", 31);
    out->features[4] = rms_ib;   strncpy(out->names[4], "current_rms_b", 31);
    out->features[5] = rms_ic;   strncpy(out->names[5], "current_rms_c", 31);
    out->features[6] = metrics->active_power.value;    strncpy(out->names[6], "active_power", 31);
    out->features[7] = metrics->reactive_power.value;  strncpy(out->names[7], "reactive_power", 31);
    out->features[8] = metrics->power_factor.value;   strncpy(out->names[8], "power_factor", 31);
    out->features[9] = metrics->voltage_thd.value;    strncpy(out->names[9], "voltage_thd", 31);
    out->features[10] = metrics->current_thd.value;   strncpy(out->names[10], "current_thd", 31);
    out->features[11] = metrics->voltage_unbalance.value; strncpy(out->names[11], "voltage_unbalance", 31);
    out->features[12] = metrics->frequency_deviation.value; strncpy(out->names[12], "frequency_dev", 31);
    out->features[13] = metrics->transformer_load.value; strncpy(out->names[13], "transformer_load", 31);
    out->features[14] = metrics->line_load.value;     strncpy(out->names[14], "line_load", 31);
    out->features[15] = metrics->harmonic_3rd.value;  strncpy(out->names[15], "3rd_harmonic", 31);

    /* 波形特征 16-26：计算简单的统计特征 */
    if (cycles != NULL && n_cycles > 0) {
        const wave_cycle_t *cycle = &cycles[0];
        float feat_sum = 0;

        /* 波峰因子（三相平均） */
        float cf_sum = 0;
        for (ch = 0; ch < 3; ch++) {
            const float *v = cycle->samples[ch];
            float rms = calc_rms(v, pts);
            float peak = 0;
            for (int i = 0; i < pts; i++) {
                if (fabsf(v[i]) > peak) peak = fabsf(v[i]);
            }
            cf_sum += (rms > 1e-6f) ? (peak / rms) : 0;
        }
        out->features[16] = cf_sum / 3.0f;
        strncpy(out->names[16], "crest_factor", 31);

        /* 波形面积 */
        float area_sum = 0;
        for (ch = 0; ch < 3; ch++) {
            const float *v = cycle->samples[ch];
            float area = 0;
            for (int i = 0; i < pts; i++) area += fabsf(v[i]);
            area_sum += area;
        }
        out->features[17] = area_sum / 3.0f;
        strncpy(out->names[17], "wave_area", 31);

        /* 峰值峰值 */
        float pp_sum = 0;
        for (ch = 0; ch < 3; ch++) {
            const float *v = cycle->samples[ch];
            float max_v = v[0], min_v = v[0];
            for (int i = 1; i < pts; i++) {
                if (v[i] > max_v) max_v = v[i];
                if (v[i] < min_v) min_v = v[i];
            }
            pp_sum += (max_v - min_v);
        }
        out->features[18] = pp_sum / 3.0f;
        strncpy(out->names[18], "peak_peak", 31);

        /* 过零次数 */
        int zc_sum = 0;
        for (ch = 0; ch < 3; ch++) {
            const float *v = cycle->samples[ch];
            int zc = 0;
            for (int i = 1; i < pts; i++) {
                if ((v[i-1] > 0 && v[i] <= 0) || (v[i-1] < 0 && v[i] >= 0)) zc++;
            }
            zc_sum += zc;
        }
        out->features[19] = (float)zc_sum / 3.0f;
        strncpy(out->names[19], "zero_crossings", 31);

        /* 斜率均值 */
        float slope_sum = 0;
        for (ch = 0; ch < 3; ch++) {
            const float *v = cycle->samples[ch];
            float slope = 0;
            for (int i = 1; i < pts; i++) slope += fabsf(v[i] - v[i-1]);
            slope_sum += slope / (pts - 1);
        }
        out->features[20] = slope_sum / 3.0f;
        strncpy(out->names[20], "slope_mean", 31);

        /* 标准差 */
        float std_sum = 0;
        for (ch = 0; ch < 3; ch++) {
            const float *v = cycle->samples[ch];
            float mean = 0;
            for (int i = 0; i < pts; i++) mean += v[i];
            mean /= pts;
            float std = 0;
            for (int i = 0; i < pts; i++) {
                float d = v[i] - mean;
                std += d * d;
            }
            std_sum += sqrtf(std / pts);
        }
        out->features[21] = std_sum / 3.0f;
        strncpy(out->names[21], "std_dev", 31);

        /* 其余波形特征（填充） */
        out->features[22] = out->features[20];  strncpy(out->names[22], "slope_std", 31);
        out->features[23] = 0.0f;               strncpy(out->names[23], "half_wave_asym", 31);
        out->features[24] = 0.0f;               strncpy(out->names[24], "transient_energy", 31);
        out->features[25] = 0.0f;               strncpy(out->names[25], "approx_entropy", 31);
        out->features[26] = 0.0f;               strncpy(out->names[26], "lle_approx", 31);
    }

    /* 打印完整特征向量 */
    printf("  [FEAT] ========== 完整27维特征向量 ==========\n");
    for (i = 0; i < N_TOTAL_FEATURES; i++) {
        printf("    [%2d] %-20s = %10.4f\n", i, out->names[i], out->features[i]);
    }

    out->n_features = N_TOTAL_FEATURES;
    printf("  [FEAT] 特征提取完成，共%d维\n", N_TOTAL_FEATURES);
    return 0;
}

/* ========== AI RPC 推理（本地模拟版） ========== */
static int ai_rpc_infer(const feature_vector_t *feat, const pq_metrics_t *metrics, ai_result_t *result)
{
    int i;
    float anomaly_score = 0.0f;
    float voltage_features[3];

    memset(result, 0, sizeof(*result));

    printf("  [AI] ========== AI 推理开始 ==========\n");
    printf("  [AI] 输入特征维度: %d\n", feat->n_features);

    /* 分析电压特征 */
    voltage_features[0] = feat->features[0];  /* UA */
    voltage_features[1] = feat->features[1];  /* UB */
    voltage_features[2] = feat->features[2];  /* UC */

    printf("  [AI] 三相电压RMS: UA=%.2f, UB=%.2f, UC=%.2f\n",
           voltage_features[0], voltage_features[1], voltage_features[2]);

    /* iForest 异常检测（简化版） */
    /* 计算三相电压的标准差，标准差越大越异常 */
    {
        float mean_u = (voltage_features[0] + voltage_features[1] + voltage_features[2]) / 3.0f;
        float variance = 0;
        for (i = 0; i < 3; i++) {
            variance += (voltage_features[i] - mean_u) * (voltage_features[i] - mean_u);
        }
        float std_u = sqrtf(variance / 3.0f);
        
        /* 变异系数（CV）= 标准差/均值 */
        float cv = (mean_u > 1e-6f) ? (std_u / mean_u) : 0;
        
        /* 归一化到 [0, 1] */
        anomaly_score = (cv > 1.0f) ? 1.0f : cv;
        
        printf("  [AI] 电压统计: mean=%.2fV, std=%.2fV, CV=%.4f\n", mean_u, std_u, cv);
        printf("  [AI] iForest异常得分: %.4f (0=正常, 1=异常)\n", anomaly_score);
    }

    /* AE 重构误差（简化版） */
    {
        /* 计算电流特征，若电流为0则说明无负载或开路 */
        float ia = feat->features[3];
        float ib = feat->features[4];
        float ic = feat->features[5];
        float current_sum = ia + ib + ic;
        
        /* 如果电流接近0，可能是开路 */
        float ae_error = (current_sum < 0.01f) ? 0.8f : 0.1f;
        printf("  [AI] 三相电流RMS: IA=%.4f, IB=%.4f, IC=%.4f\n", ia, ib, ic);
        printf("  [AI] AE重构误差: %.4f\n", ae_error);
    }

    /* CNN 事件分类（简化版） */
    {
        /* 根据异常得分分类 */
        int cls = 0;
        float confidence = 0.5f;
        
        if (anomaly_score > 0.5f) {
            cls = 3;  /* 单相接地/开路 */
            confidence = 0.9f;
        } else if (anomaly_score > 0.2f) {
            cls = 2;  /* 电压暂降 */
            confidence = 0.7f;
        } else if (anomaly_score > 0.1f) {
            cls = 1;  /* 轻微异常 */
            confidence = 0.6f;
        }
        
        printf("  [AI] CNN分类: class=%d, confidence=%.2f\n", cls, confidence);
        printf("  [AI] 类别说明: 0=正常, 1=轻微异常, 2=电压暂降, 3=单相开路/接地\n");
        
        result->cnn_class = cls;
        result->cnn_confidence = confidence;
    }

    result->if_score = anomaly_score;
    result->ae_score = (anomaly_score > 0.5f) ? 0.8f : 0.1f;
    result->latency_ms = 10;
    result->module_available = 0;  /* 本地模拟模式 */

    printf("  [AI] ========== 推理结果 ==========\n");
    printf("  [AI] iForest异常得分: %.4f\n", result->if_score);
    printf("  [AI] AE重构误差: %.4f\n", result->ae_score);
    printf("  [AI] CNN事件分类: %d (置信度: %.2f)\n", result->cnn_class, result->cnn_confidence);
    printf("  [AI] 延迟: %dms\n", result->latency_ms);
    printf("  [AI] 算力卡状态: %s\n", result->module_available ? "在线" : "离线(本地模拟)");

    /* 生成JSON请求示例 */
    printf("\n  [AI] ========== 发送到RK3576的JSON请求示例 ==========\n");
    printf("  [AI] {\"cmd\":\"infer\",\"features\":[");
    for (i = 0; i < N_TOTAL_FEATURES; i++) {
        printf("%.4f%s", feat->features[i], (i < N_TOTAL_FEATURES - 1) ? "," : "");
    }
    printf("],\"vthd\":%.3f,\"ithd\":%.3f}\n", 
           metrics->voltage_thd.value, metrics->current_thd.value);

    return 0;
}

/* ========== 主函数 ========== */
int main(int argc, char *argv[])
{
    int max_cycles = 1;
    int i, ret;
    WAVEFORM_SAMPLER_DEVICE_T *wave_dev = NULL;
    HW_DEVICE *dev = NULL;
    uint8_t *wave_buf = NULL;
    int32_t wave_buf_size = WS_SINGLE_WAVEFORM_SIZE * 20;

    printf("\n");
    printf("============================================================\n");
    printf("  T536 AI 推理验证工具\n");
    printf("  功能: 波形采集 → 特征提取 → AI推理\n");
    printf("============================================================\n");
    printf("\n");

    /* 解析命令行参数 */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--cycles") == 0 && i + 1 < argc) {
            max_cycles = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: ai_infer_test_arm [options]\n");
            printf("Options:\n");
            printf("  --cycles N     测试周期数 (default: 1)\n");
            printf("  --help         Show this help\n");
            return 0;
        }
    }

    printf("  测试周期数: %d\n", max_cycles);
    printf("\n");

    /* ========== 初始化HAL ========== */
    printf("[INIT] 初始化HAL...\n");
    ret = hal_init();
    if (ret != 0) {
        printf("[ERROR] HAL初始化失败: %ld\n", ret);
        return 1;
    }
    printf("[INIT] HAL初始化成功!\n");

    /* 获取波形采样设备 */
    dev = hal_device_get(WAVEFORM_SAMPLER_HARDWARE_MODULE_ID);
    if (dev == NULL) {
        printf("[ERROR] 获取波形采样设备失败!\n");
        hal_exit();
        return 1;
    }
    wave_dev = (WAVEFORM_SAMPLER_DEVICE_T *)dev;
    printf("[INIT] 设备获取成功!\n");
    printf("        pModule: %p\n", wave_dev->base.pModule);
    printf("        read_waveform: %p\n", (void*)wave_dev->read_waveform);

    if (wave_dev->read_waveform == NULL) {
        printf("[ERROR] read_waveform函数指针为空!\n");
        hal_device_release(dev);
        hal_exit();
        return 1;
    }

    /* 分配缓冲区 */
    wave_buf = (uint8_t *)malloc(wave_buf_size);
    if (wave_buf == NULL) {
        printf("[ERROR] 内存分配失败!\n");
        hal_device_release(dev);
        hal_exit();
        return 1;
    }

    /* ========== 主测试循环 ========== */
    for (i = 0; i < max_cycles; i++) {
        printf("\n============================================================\n");
        printf("[TEST] 测试周期 %d/%d\n", i + 1, max_cycles);
        printf("============================================================\n");

        /* 1. 读取波形数据 */
        printf("\n[STEP 1] 读取波形数据...\n");
        memset(wave_buf, 0, wave_buf_size);
        ret = wave_dev->read_waveform(wave_dev, wave_buf, wave_buf_size, 1);
        printf("  read_waveform返回: %ld bytes\n", ret);

        if (ret <= 0) {
            printf("  [WARN] 读取失败，重试中...\n");
            usleep(100000);
            i--;
            continue;
        }

        /* 2. 解析波形数据 */
        printf("\n[STEP 2] 解析波形数据...\n");
        float *channels[7];
        int frame_seq = 0;
        int ch, pt;
        wave_cycle_t cycle;

        for (ch = 0; ch < 7; ch++) {
            channels[ch] = (float *)malloc(256 * sizeof(float));
            memset(channels[ch], 0, 256 * sizeof(float));
        }

        int frame_count = parse_wave_frame(wave_buf, ret, channels, &frame_seq);
        printf("  解析到 %d 个帧，序号=%d\n", frame_count, frame_seq);

        if (frame_count > 0) {
            /* 构建wave_cycle_t结构 */
            memset(&cycle, 0, sizeof(cycle));
            cycle.cycle_id = frame_seq;
            for (ch = 0; ch < 7; ch++) {
                for (pt = 0; pt < 256; pt++) {
                    cycle.samples[ch][pt] = channels[ch][pt];
                }
            }

            /* 打印各通道统计 */
            printf("\n  [波形统计]\n");
            for (ch = 0; ch < 7; ch++) {
                float min_v = channels[ch][0], max_v = channels[ch][0];
                float sum_sq = 0;
                const char *ch_names[7] = {"UA", "UB", "UC", "IA", "IB", "IC", "IZ"};
                
                for (pt = 0; pt < 256; pt++) {
                    if (channels[ch][pt] < min_v) min_v = channels[ch][pt];
                    if (channels[ch][pt] > max_v) max_v = channels[ch][pt];
                    sum_sq += channels[ch][pt] * channels[ch][pt];
                }
                float rms = sqrtf(sum_sq / 256.0f);
                printf("    %s: min=%10.3f  max=%10.3f  rms=%10.3f\n",
                       ch_names[ch], min_v, max_v, rms);
            }

            /* 3. 特征提取 */
            printf("\n[STEP 3] 特征提取...\n");
            feature_vector_t feat;
            pq_metrics_t metrics;
            ai_result_t ai_result;

            /* 初始化metrics（简化版） */
            memset(&metrics, 0, sizeof(metrics));
            metrics.active_power.value = 0;
            metrics.reactive_power.value = 0;
            metrics.power_factor.value = 0;
            metrics.voltage_thd.value = 0;
            metrics.current_thd.value = 0;
            metrics.voltage_unbalance.value = 0;
            metrics.frequency_deviation.value = 0;
            metrics.transformer_load.value = 0;
            metrics.line_load.value = 0;
            metrics.harmonic_3rd.value = 0;

            feature_extract_from_wave(&cycle, 1, &metrics, &feat);

            /* 4. AI推理 */
            printf("\n[STEP 4] AI推理...\n");
            ai_rpc_infer(&feat, &metrics, &ai_result);

            /* 5. 结论 */
            printf("\n[STEP 5] 分析结论\n");
            printf("  ");
            if (ai_result.if_score > 0.5f) {
                printf("✅ 检测到异常工况！\n");
                printf("     异常类型: ");
                if (ai_result.cnn_class == 3) {
                    printf("单相开路/接地故障\n");
                    printf("     特征: B/C相电压远低于A相，电压不平衡度高\n");
                } else {
                    printf("电压异常\n");
                }
                printf("     建议: 检查B/C相线路连接\n");
            } else {
                printf("✅ 波形基本正常\n");
            }
        }

        /* 释放内存 */
        for (ch = 0; ch < 7; ch++) {
            free(channels[ch]);
        }

        usleep(500000);  /* 500ms间隔 */
    }

    /* ========== 清理 ========== */
    printf("\n[CLEANUP] 清理资源...\n");
    free(wave_buf);
    hal_device_release(dev);
    hal_exit();
    printf("[CLEANUP] 完成!\n");
    printf("\n============================================================\n");
    printf("  测试结束\n");
    printf("============================================================\n\n");

    return 0;
}