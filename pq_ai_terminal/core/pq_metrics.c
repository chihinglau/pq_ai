/**
 * @file pq_metrics.c
 * @brief 标准电能质量指标计算模块实现
 * @author PQ AI Terminal Team
 * @date 2026-08-02
 */

#include "pq_metrics.h"
#include <math.h>
#include <string.h>

static float g_v_nom = PQ_NOMINAL_VOLTAGE;
static float g_f_nom = PQ_NOMINAL_FREQ;

/* 国标限值（默认可通过pq_metrics_set_limits覆盖） */
static float g_limit_voltage_deviation   = 7.0f;
static float g_limit_voltage_thd         = 5.0f;
static float g_limit_current_thd         = 8.0f;
static float g_limit_voltage_unbalance   = 2.0f;
static float g_limit_frequency_deviation = 0.5f;
static float g_limit_transformer_load    = 100.0f;
static float g_limit_line_load           = 100.0f;
static float g_limit_power_factor        = 0.85f; /* 工业负荷正常范围 */
static float g_s_rated_kva               = 800.0f;
static float g_i_max_a                   = 380.0f;

/**
 * @brief 初始化单个指标结构体
 */
static void metric_init(pq_metric_t *m, const char *name, float limit)
{
    memset(m, 0, sizeof(pq_metric_t));
    strncpy(m->name, name, sizeof(m->name) - 1);
    m->limit = limit;
    m->status = 0;
    m->confidence = 1.0f;
}

/**
 * @brief 评估单个指标状态
 */
static void metric_evaluate(pq_metric_t *m)
{
    float abs_val = fabsf(m->value);
    if (abs_val > m->limit) {
        m->status = 2; /* 超标 */
    } else if (abs_val > m->limit * 0.9f) {
        m->status = 1; /* 预警 */
    } else {
        m->status = 0; /* 正常 */
    }
}

int pq_metrics_init(float v_nom, float f_nom)
{
    g_v_nom = v_nom;
    g_f_nom = f_nom;
    return 0;
}

void pq_metrics_set_limits(float voltage_deviation, float voltage_thd, float current_thd,
                           float voltage_unbalance, float frequency_deviation,
                           float transformer_load, float line_load, float power_factor)
{
    g_limit_voltage_deviation   = voltage_deviation;
    g_limit_voltage_thd         = voltage_thd;
    g_limit_current_thd         = current_thd;
    g_limit_voltage_unbalance   = voltage_unbalance;
    g_limit_frequency_deviation = frequency_deviation;
    g_limit_transformer_load    = transformer_load;
    g_limit_line_load           = line_load;
    g_limit_power_factor        = power_factor;
}

/**
 * @brief 从波形计算三相不平衡度（简化版，使用RMS近似）
 */
static float calc_unbalance_rms(const ht7627s_regs_t *regs)
{
    float va = regs->rms[0];
    float vb = regs->rms[1];
    float vc = regs->rms[2];
    float v_avg = (va + vb + vc) / 3.0f;
    float v_max_dev = fabsf(va - v_avg);
    float dev;

    dev = fabsf(vb - v_avg);
    if (dev > v_max_dev) v_max_dev = dev;
    dev = fabsf(vc - v_avg);
    if (dev > v_max_dev) v_max_dev = dev;

    if (v_avg > 1.0f) {
        return (v_max_dev / v_avg) * 100.0f;
    }
    return 0.0f;
}

int pq_metrics_calculate(const ht7627s_regs_t *regs, const ht7627s_wave_t *wave, pq_metrics_t *out)
{
    float v_rms_avg;
    float s_apparent;

    if (regs == NULL || out == NULL) {
        return -1;
    }

    (void)wave; /* 波形数据预留，当前用寄存器数据计算 */

    memset(out, 0, sizeof(pq_metrics_t));

    /* 1. 电压偏差 % */
    v_rms_avg = (regs->rms[0] + regs->rms[1] + regs->rms[2]) / 3.0f;
    metric_init(&out->voltage_deviation, "voltage_deviation", g_limit_voltage_deviation);
    out->voltage_deviation.value = ((v_rms_avg - g_v_nom) / g_v_nom) * 100.0f;

    /* 2. 电压THD % */
    metric_init(&out->voltage_thd, "voltage_thd", g_limit_voltage_thd);
    out->voltage_thd.value = regs->thd_v;

    /* 3. 电流THD % */
    metric_init(&out->current_thd, "current_thd", g_limit_current_thd);
    out->current_thd.value = regs->thd_i;

    /* 4. 电压不平衡度 % */
    metric_init(&out->voltage_unbalance, "voltage_unbalance", g_limit_voltage_unbalance);
    out->voltage_unbalance.value = calc_unbalance_rms(regs);

    /* 5. 频率偏差 Hz */
    metric_init(&out->frequency_deviation, "frequency_deviation", g_limit_frequency_deviation);
    out->frequency_deviation.value = fabsf(regs->frequency - g_f_nom);

    /* 6. 变压器负载率 % */
    metric_init(&out->transformer_load, "transformer_load", g_limit_transformer_load);
    s_apparent = regs->apparent_power;
    out->transformer_load.value = (s_apparent / g_s_rated_kva) * 100.0f;

    /* 7. 线路负载率 % */
    metric_init(&out->line_load, "line_load", g_limit_line_load);
    {
        float i_rms_avg = (regs->rms[3] + regs->rms[4] + regs->rms[5]) / 3.0f;
        out->line_load.value = (i_rms_avg / g_i_max_a) * 100.0f;
    }

    /* 8. 功率因数 */
    metric_init(&out->power_factor, "power_factor", g_limit_power_factor);
    out->power_factor.value = regs->power_factor;

    /* 9. 有功功率 kW */
    metric_init(&out->active_power, "active_power", 1e6f);
    out->active_power.value = regs->active_power;

    /* 10. 无功功率 kvar */
    metric_init(&out->reactive_power, "reactive_power", 1e6f);
    out->reactive_power.value = regs->reactive_power;

    /* 11. 视在功率 kVA */
    metric_init(&out->apparent_power, "apparent_power", 1e6f);
    out->apparent_power.value = regs->apparent_power;

    /* 12. 3次谐波含有率 % */
    metric_init(&out->harmonic_3rd, "harmonic_3rd", g_limit_voltage_thd);
    out->harmonic_3rd.value = regs->harmonics[2]; /* 3次谐波在索引2 */

    return 0;
}

int pq_metrics_evaluate(pq_metrics_t *metrics)
{
    if (metrics == NULL) {
        return -1;
    }

    metric_evaluate(&metrics->voltage_deviation);
    metric_evaluate(&metrics->voltage_thd);
    metric_evaluate(&metrics->current_thd);
    metric_evaluate(&metrics->voltage_unbalance);
    metric_evaluate(&metrics->frequency_deviation);
    metric_evaluate(&metrics->transformer_load);
    metric_evaluate(&metrics->line_load);

    /* 功率因数评估：低于limit超标，低于limit*1.06预警 */
    if (metrics->power_factor.value < g_limit_power_factor) {
        metrics->power_factor.status = 2;
    } else if (metrics->power_factor.value < g_limit_power_factor * 1.06f) {
        metrics->power_factor.status = 1;
    } else {
        metrics->power_factor.status = 0;
    }

    metric_evaluate(&metrics->harmonic_3rd);

    return 0;
}

const char* pq_metric_status_str(int status)
{
    switch (status) {
        case 0:  return "NORMAL";
        case 1:  return "WARNING";
        case 2:  return "ALARM";
        default: return "UNKNOWN";
    }
}
