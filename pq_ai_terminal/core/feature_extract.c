/**
 * @file feature_extract.c
 * @brief 特征工程模块实现
 */

#include "feature_extract.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

static void set_feature(feature_vector_t *out, int idx, const char *name, float val)
{
    out->features[idx] = val;
    strncpy(out->names[idx], name, sizeof(out->names[idx]) - 1);
}

static float calc_rms(const float *x, int n)
{
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        sum += x[i] * x[i];
    }
    return sqrtf(sum / (float)n);
}

static float calc_mean(const float *x, int n)
{
    float sum = 0.0f;
    for (int i = 0; i < n; i++) sum += x[i];
    return sum / (float)n;
}

static float calc_std(const float *x, int n, float mean)
{
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        float d = x[i] - mean;
        sum += d * d;
    }
    return sqrtf(sum / (float)n);
}

static float calc_crest_factor(const float *x, int n)
{
    float peak = 0.0f;
    float rms = calc_rms(x, n);
    for (int i = 0; i < n; i++) {
        float a = fabsf(x[i]);
        if (a > peak) peak = a;
    }
    return (rms > 1e-6f) ? (peak / rms) : 0.0f;
}

static float calc_form_factor(const float *x, int n)
{
    float rms = calc_rms(x, n);
    float sum_abs = 0.0f;
    for (int i = 0; i < n; i++) sum_abs += fabsf(x[i]);
    float mean_abs = sum_abs / (float)n;
    return (mean_abs > 1e-6f) ? (rms / mean_abs) : 0.0f;
}

static float calc_wave_area(const float *x, int n)
{
    float sum = 0.0f;
    for (int i = 0; i < n; i++) sum += fabsf(x[i]);
    return sum;
}

static float calc_slope_mean(const float *x, int n)
{
    float sum = 0.0f;
    for (int i = 1; i < n; i++) {
        sum += fabsf(x[i] - x[i - 1]);
    }
    return sum / (float)(n - 1);
}

static float calc_slope_std(const float *x, int n, float slope_mean)
{
    float sum = 0.0f;
    for (int i = 1; i < n; i++) {
        float d = fabsf(x[i] - x[i - 1]) - slope_mean;
        sum += d * d;
    }
    return sqrtf(sum / (float)(n - 1));
}

static int calc_zero_crossings(const float *x, int n)
{
    int count = 0;
    for (int i = 1; i < n; i++) {
        if ((x[i - 1] > 0.0f && x[i] <= 0.0f) || (x[i - 1] < 0.0f && x[i] >= 0.0f)) {
            count++;
        }
    }
    return count;
}

static float calc_peak_peak(const float *x, int n)
{
    float max_v = x[0], min_v = x[0];
    for (int i = 1; i < n; i++) {
        if (x[i] > max_v) max_v = x[i];
        if (x[i] < min_v) min_v = x[i];
    }
    return max_v - min_v;
}

static float calc_half_wave_asymmetry(const float *x, int n)
{
    float pos_sum = 0.0f, neg_sum = 0.0f;
    int pos_n = 0, neg_n = 0;
    for (int i = 0; i < n; i++) {
        if (x[i] >= 0.0f) {
            pos_sum += x[i];
            pos_n++;
        } else {
            neg_sum += -x[i];
            neg_n++;
        }
    }
    if (pos_n == 0 || neg_n == 0) return 0.0f;
    float pos_avg = pos_sum / pos_n;
    float neg_avg = neg_sum / neg_n;
    return fabsf(pos_avg - neg_avg) / (pos_avg + neg_avg + 1e-6f);
}

static float calc_transient_energy(const float *x, int n)
{
    float sum = 0.0f;
    for (int i = 1; i < n; i++) {
        float d = x[i] - x[i - 1];
        sum += d * d;
    }
    return sum;
}

int feature_extract_init(void)
{
    return 0;
}

int feature_extract_from_wave(const wave_cycle_t *cycles, int n_cycles,
                              const pq_metrics_t *metrics, feature_vector_t *out)
{
    int i, ch;
    float wave_feat[11] = {0};
    int pts = PQ_POINTS_PER_CYCLE_12800; /* 实际硬件每周期256点 */
    float rms_ua = 0, rms_ub = 0, rms_uc = 0;
    float rms_ia = 0, rms_ib = 0, rms_ic = 0;

    if (out == NULL || metrics == NULL) return -1;
    memset(out, 0, sizeof(feature_vector_t));

    /* ========== 从波形数据直接计算各相RMS ========== */
    if (cycles != NULL && n_cycles > 0) {
        const wave_cycle_t *cycle = &cycles[0];

        /* 安全限制：使用实际采样点数 */
        pts = (pts <= PQ_POINTS_PER_CYCLE_25600) ? pts : PQ_POINTS_PER_CYCLE_25600;

        /* 计算三相电压RMS (channel 0=UA, 1=UB, 2=UC) */
        rms_ua = calc_rms(cycle->samples[0], pts);
        rms_ub = calc_rms(cycle->samples[1], pts);
        rms_uc = calc_rms(cycle->samples[2], pts);

        /* 计算三相电流RMS (channel 3=IA, 4=IB, 5=IC) */
        rms_ia = calc_rms(cycle->samples[3], pts);
        rms_ib = calc_rms(cycle->samples[4], pts);
        rms_ic = calc_rms(cycle->samples[5], pts);

        PQ_LOGI("feature_extract: 波形RMS计算结果:");
        PQ_LOGI("  UA=%.3fV, UB=%.3fV, UC=%.3fV", rms_ua, rms_ub, rms_uc);
        PQ_LOGI("  IA=%.3fA, IB=%.3fA, IC=%.3fA", rms_ia, rms_ib, rms_ic);
    } else {
        /* 无波形数据时使用metrics兜底 */
        rms_ua = metrics->voltage_deviation.value;
        rms_ub = metrics->voltage_deviation.value;
        rms_uc = metrics->voltage_deviation.value;
        rms_ia = metrics->line_load.value;
        rms_ib = metrics->line_load.value;
        rms_ic = metrics->line_load.value;
    }

    /* ========== 标准特征 0-15 ========== */
    set_feature(out, 0,  "voltage_rms_a",    rms_ua);
    set_feature(out, 1,  "voltage_rms_b",    rms_ub);
    set_feature(out, 2,  "voltage_rms_c",    rms_uc);
    set_feature(out, 3,  "current_rms_a",    rms_ia);
    set_feature(out, 4,  "current_rms_b",    rms_ib);
    set_feature(out, 5,  "current_rms_c",    rms_ic);
    set_feature(out, 6,  "active_power",     metrics->active_power.value);
    set_feature(out, 7,  "reactive_power",   metrics->reactive_power.value);
    set_feature(out, 8,  "power_factor",     metrics->power_factor.value);
    set_feature(out, 9,  "voltage_thd",      metrics->voltage_thd.value);
    set_feature(out, 10, "current_thd",      metrics->current_thd.value);
    set_feature(out, 11, "voltage_unbalance", metrics->voltage_unbalance.value);
    set_feature(out, 12, "frequency_dev",    metrics->frequency_deviation.value);
    set_feature(out, 13, "transformer_load", metrics->transformer_load.value);
    set_feature(out, 14, "line_load",        metrics->line_load.value);
    set_feature(out, 15, "3rd_harmonic",     metrics->harmonic_3rd.value);

    /* ========== 波形特征 16-26：分析三相电压 ========== */
    if (cycles != NULL && n_cycles > 0) {
        const wave_cycle_t *cycle = &cycles[0];
        float feat_sum[11] = {0};
        int feat_count = 0;

        /* 对三个电压通道 (UA, UB, UC) 分别计算特征，然后取平均 */
        for (ch = 0; ch < 3; ch++) {
            const float *v = cycle->samples[ch];
            float ch_feat[11];

            ch_feat[0] = calc_crest_factor(v, pts);
            ch_feat[1] = calc_form_factor(v, pts);
            ch_feat[2] = calc_wave_area(v, pts);
            ch_feat[3] = calc_slope_mean(v, pts);
            ch_feat[4] = calc_slope_std(v, pts, ch_feat[3]);
            ch_feat[5] = (float)calc_zero_crossings(v, pts);
            ch_feat[6] = calc_peak_peak(v, pts);
            ch_feat[7] = calc_half_wave_asymmetry(v, pts);
            ch_feat[8] = calc_transient_energy(v, pts);

            float mean = calc_mean(v, pts);
            ch_feat[9] = calc_std(v, pts, mean);
            ch_feat[10] = ch_feat[3];

            for (i = 0; i < 11; i++) {
                feat_sum[i] += ch_feat[i];
            }
            feat_count++;

            PQ_LOGI("feature_extract: 通道%d(%s)波形特征: crest=%.3f, form=%.3f, area=%.3f, zc=%.0f",
                   ch, (ch == 0) ? "UA" : (ch == 1) ? "UB" : "UC",
                   ch_feat[0], ch_feat[1], ch_feat[2], ch_feat[5]);
        }

        /* 取三相平均作为最终波形特征 */
        if (feat_count > 0) {
            for (i = 0; i < 11; i++) {
                wave_feat[i] = feat_sum[i] / feat_count;
            }
        }

        PQ_LOGI("feature_extract: 综合波形特征(三相平均):");
        PQ_LOGI("  crest_factor=%.3f, form_factor=%.3f, wave_area=%.3f",
               wave_feat[0], wave_feat[1], wave_feat[2]);
        PQ_LOGI("  slope_mean=%.3f, slope_std=%.3f, zero_crossings=%.0f",
               wave_feat[3], wave_feat[4], wave_feat[5]);
        PQ_LOGI("  peak_peak=%.3f, half_wave_asym=%.3f, transient_energy=%.3f",
               wave_feat[6], wave_feat[7], wave_feat[8]);
    }

    for (i = 0; i < 11; i++) {
        set_feature(out, 16 + i, "wave_feat", wave_feat[i]);
    }

    /* ========== 打印完整特征向量 ========== */
    PQ_LOGI("feature_extract: 完整27维特征向量:");
    for (i = 0; i < N_TOTAL_FEATURES; i++) {
        PQ_LOGI("  [%2d] %-20s = %10.4f", i, out->names[i], out->features[i]);
    }

    out->n_features = N_TOTAL_FEATURES;
    return 0;
}
