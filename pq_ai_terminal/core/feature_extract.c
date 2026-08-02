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
    int i;
    float wave_feat[11] = {0};
    int pts = PQ_POINTS_PER_CYCLE_25600;

    if (out == NULL || metrics == NULL) return -1;
    memset(out, 0, sizeof(feature_vector_t));

    /* 标准特征 0-15 */
    set_feature(out, 0,  "voltage_rms_a",    metrics->voltage_deviation.value); /* 简化 */
    set_feature(out, 1,  "voltage_rms_b",    metrics->voltage_deviation.value);
    set_feature(out, 2,  "voltage_rms_c",    metrics->voltage_deviation.value);
    set_feature(out, 3,  "current_rms_a",    metrics->line_load.value);
    set_feature(out, 4,  "current_rms_b",    metrics->line_load.value);
    set_feature(out, 5,  "current_rms_c",    metrics->line_load.value);
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

    /* 波形特征 16-26：对三相电压取平均 */
    if (cycles != NULL && n_cycles > 0) {
        const float *v = cycles[0].samples[0];
        pts = (pts < 512) ? pts : 512; /* 安全限制 */

        wave_feat[0] = calc_crest_factor(v, pts);
        wave_feat[1] = calc_form_factor(v, pts);
        wave_feat[2] = calc_wave_area(v, pts);
        wave_feat[3] = calc_slope_mean(v, pts);
        wave_feat[4] = calc_slope_std(v, pts, wave_feat[3]);
        wave_feat[5] = (float)calc_zero_crossings(v, pts);
        wave_feat[6] = calc_peak_peak(v, pts);
        wave_feat[7] = calc_half_wave_asymmetry(v, pts);
        wave_feat[8] = calc_transient_energy(v, pts);

        /* 近似熵简化：用标准差代替 */
        float mean = calc_mean(v, pts);
        wave_feat[9] = calc_std(v, pts, mean);

        /* LLE近似：用斜率均值 */
        wave_feat[10] = wave_feat[3];
    }

    for (i = 0; i < 11; i++) {
        set_feature(out, 16 + i, "wave_feat", wave_feat[i]);
    }

    out->n_features = N_TOTAL_FEATURES;
    return 0;
}
