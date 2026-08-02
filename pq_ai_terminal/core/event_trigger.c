/**
 * @file event_trigger.c
 * @brief 事件触发引擎实现
 * @author PQ AI Terminal Team
 * @date 2026-08-02
 */

#include "event_trigger.h"
#include <math.h>
#include <string.h>
#include <time.h>

/* 滞回系数 */
#define HYST_ENTER  0.90f
#define HYST_EXIT   0.92f

/* 静态事件状态记录（滞回） */
static struct {
    uint8_t voltage_dev;
    uint8_t voltage_thd;
    uint8_t current_thd;
    uint8_t unbalance;
    uint8_t freq_dev;
    uint8_t transformer_load;
    uint8_t line_load;
} g_event_state;

static uint32_t g_event_id = 1;

int event_trigger_init(void)
{
    memset(&g_event_state, 0, sizeof(g_event_state));
    g_event_id = 1;
    return 0;
}

/**
 * @brief 计算严重度
 */
static float calc_severity(float value, float limit)
{
    float ratio;
    if (limit <= 0.0f) {
        return 0.0f;
    }
    ratio = fabsf(value) / limit;
    if (ratio > 1.0f) {
        return (ratio > 10.0f) ? 10.0f : ratio;
    }
    return ratio;
}

/**
 * @brief 带滞回的状态检查
 * @param active 当前是否满足触发条件
 * @param state 状态记录指针
 * @param enter_thr 进入阈值
 * @param exit_thr 退出阈值
 * @return 1触发，0未触发
 */
static int hysteresis_check(int active, uint8_t *state, float enter_thr, float exit_thr)
{
    if (active) {
        if (*state == 0) {
            /* 未激活，检查进入条件 */
            if (enter_thr >= 0.0f) {
                *state = 1;
                return 1;
            }
        } else {
            /* 已激活，保持 */
            return 1;
        }
    } else {
        if (*state == 1) {
            /* 已激活，检查退出条件 */
            if (exit_thr >= 0.0f) {
                *state = 0;
            } else {
                return 1;
            }
        }
    }
    return 0;
}

/**
 * @brief 填充事件结构体
 */
static void fill_event(pq_event_t *evt, uint8_t type, const pq_metric_t *metric,
                       const char *desc, float severity)
{
    memset(evt, 0, sizeof(pq_event_t));
    evt->id = g_event_id++;
    evt->type = type;
    evt->start_ts = (uint32_t)time(NULL);
    evt->end_ts = (uint32_t)time(NULL);
    evt->severity = severity;
    evt->phase = metric->phase;
    evt->trigger_metric = *metric;
    strncpy(evt->description, desc, sizeof(evt->description) - 1);
}

int event_trigger_check(const pq_metrics_t *metrics, pq_event_t *event_out)
{
    float severity;
    int triggered = 0;

    if (metrics == NULL || event_out == NULL) {
        return -1;
    }

    /* 1. 电压偏差 - 暂降/暂升检测 */
    {
        float v_dev = metrics->voltage_deviation.value;
        float limit = metrics->voltage_deviation.limit;
        int active_sag = (v_dev < -10.0f);
        int active_swell = (v_dev > 10.0f);
        int active_dev = (fabsf(v_dev) > limit * HYST_ENTER);

        if (active_sag || active_dev) {
            if (hysteresis_check(1, &g_event_state.voltage_dev, 1.0f, 1.0f)) {
                severity = calc_severity(v_dev, limit);
                if (active_sag) {
                    fill_event(event_out, EVENT_TYPE_VOLTAGE_SAG,
                               &metrics->voltage_deviation,
                               "Voltage sag detected", severity);
                } else {
                    fill_event(event_out, EVENT_TYPE_VOLTAGE_SWELL,
                               &metrics->voltage_deviation,
                               "Voltage deviation exceed limit", severity);
                }
                triggered = 1;
                return triggered;
            }
        } else if (fabsf(v_dev) < limit * HYST_EXIT) {
            g_event_state.voltage_dev = 0;
        }

        if (active_swell) {
            if (hysteresis_check(1, &g_event_state.voltage_dev, 1.0f, 1.0f)) {
                severity = calc_severity(v_dev, limit);
                fill_event(event_out, EVENT_TYPE_VOLTAGE_SWELL,
                           &metrics->voltage_deviation,
                           "Voltage swell detected", severity);
                triggered = 1;
                return triggered;
            }
        }
    }

    /* 2. 电压THD */
    {
        float val = metrics->voltage_thd.value;
        float limit = metrics->voltage_thd.limit;
        int active = (val > limit * HYST_ENTER);
        int exit_cond = (val < limit * HYST_EXIT);

        if (hysteresis_check(active, &g_event_state.voltage_thd,
                             active ? 1.0f : -1.0f,
                             exit_cond ? 1.0f : -1.0f)) {
            severity = calc_severity(val, limit);
            fill_event(event_out, EVENT_TYPE_HARMONIC,
                       &metrics->voltage_thd,
                       "Voltage THD exceed limit", severity);
            triggered = 1;
            return triggered;
        }
    }

    /* 3. 电流THD */
    {
        float val = metrics->current_thd.value;
        float limit = metrics->current_thd.limit;
        int active = (val > limit * HYST_ENTER);
        int exit_cond = (val < limit * HYST_EXIT);

        if (hysteresis_check(active, &g_event_state.current_thd,
                             active ? 1.0f : -1.0f,
                             exit_cond ? 1.0f : -1.0f)) {
            severity = calc_severity(val, limit);
            fill_event(event_out, EVENT_TYPE_HARMONIC,
                       &metrics->current_thd,
                       "Current THD exceed limit", severity);
            triggered = 1;
            return triggered;
        }
    }

    /* 4. 不平衡度 */
    {
        float val = metrics->voltage_unbalance.value;
        float limit = metrics->voltage_unbalance.limit;
        int active = (val > limit * HYST_ENTER);
        int exit_cond = (val < limit * HYST_EXIT);

        if (hysteresis_check(active, &g_event_state.unbalance,
                             active ? 1.0f : -1.0f,
                             exit_cond ? 1.0f : -1.0f)) {
            severity = calc_severity(val, limit);
            fill_event(event_out, EVENT_TYPE_UNBALANCE,
                       &metrics->voltage_unbalance,
                       "Voltage unbalance exceed limit", severity);
            triggered = 1;
            return triggered;
        }
    }

    /* 5. 频率偏差 */
    {
        float val = metrics->frequency_deviation.value;
        float limit = metrics->frequency_deviation.limit;
        int active = (val > limit * HYST_ENTER);
        int exit_cond = (val < limit * HYST_EXIT);

        if (hysteresis_check(active, &g_event_state.freq_dev,
                             active ? 1.0f : -1.0f,
                             exit_cond ? 1.0f : -1.0f)) {
            severity = calc_severity(val, limit);
            fill_event(event_out, EVENT_TYPE_FREQ_DEV,
                       &metrics->frequency_deviation,
                       "Frequency deviation exceed limit", severity);
            triggered = 1;
            return triggered;
        }
    }

    /* 6. 变压器负载率 */
    {
        float val = metrics->transformer_load.value;
        float limit = metrics->transformer_load.limit;
        int active = (val > limit * HYST_ENTER); /* >90% */
        int exit_cond = (val < limit * HYST_EXIT); /* <92% */

        if (hysteresis_check(active, &g_event_state.transformer_load,
                             active ? 1.0f : -1.0f,
                             exit_cond ? 1.0f : -1.0f)) {
            severity = calc_severity(val, limit);
            if (val > limit) {
                fill_event(event_out, EVENT_TYPE_OVERLOAD,
                           &metrics->transformer_load,
                           "Transformer overload", severity);
            } else {
                fill_event(event_out, EVENT_TYPE_OVERLOAD,
                           &metrics->transformer_load,
                           "Transformer load warning", severity);
            }
            triggered = 1;
            return triggered;
        }
    }

    return triggered;
}

const char* event_type_str(uint8_t type)
{
    switch (type) {
        case EVENT_TYPE_VOLTAGE_SAG:    return "VOLTAGE_SAG";
        case EVENT_TYPE_VOLTAGE_SWELL:  return "VOLTAGE_SWELL";
        case EVENT_TYPE_HARMONIC:       return "HARMONIC";
        case EVENT_TYPE_UNBALANCE:      return "UNBALANCE";
        case EVENT_TYPE_OVERLOAD:       return "OVERLOAD";
        case EVENT_TYPE_FREQ_DEV:       return "FREQ_DEVIATION";
        case EVENT_TYPE_SCENARIO_CHG:   return "SCENARIO_CHANGE";
        case EVENT_TYPE_UNKNOWN:        return "UNKNOWN";
        default:                        return "UNKNOWN";
    }
}
