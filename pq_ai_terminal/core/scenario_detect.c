/**
 * @file scenario_detect.c
 * @brief 场景识别模块实现 (S1~S5)
 */

#include "scenario_detect.h"
#include <string.h>

int scenario_detect_init(void)
{
    return 0;
}

scenario_type_t scenario_detect_classify(const pq_metrics_t *metrics,
                                         const feature_vector_t *feat)
{
    (void)feat;

    if (metrics == NULL) {
        return SCENARIO_UNKNOWN;
    }

    float thd_v = metrics->voltage_thd.value;
    float thd_i = metrics->current_thd.value;
    float unbal = metrics->voltage_unbalance.value;
    float load  = metrics->transformer_load.value;
    float v_dev = metrics->voltage_deviation.value;

    /* S5 极端场景优先级最高 */
    if (thd_v > 7.0f || thd_i > 20.0f || load > 90.0f || unbal > 3.0f) {
        return SCENARIO_S5_EXTREME;
    }

    /* S2 EV充电：电流谐波主导，负载率相对不高 */
    int is_s2 = (thd_i > 8.0f && load < 40.0f);

    /* S3 光伏：电压抬升+偶次谐波特征 */
    int is_s3 = (v_dev > 2.0f && thd_v > 1.5f && thd_v < 5.0f && thd_i < 5.0f);

    /* S4 光充耦合：同时有电压抬升和谐波电流 */
    int is_s4 = (thd_v > 1.5f && thd_v < 8.0f && v_dev > 1.0f && thd_i > 5.0f);

    if (is_s4 && (is_s2 || is_s3)) {
        return SCENARIO_S4_EV_PV;
    }
    if (is_s2) {
        return SCENARIO_S2_EV_CHARGING;
    }
    if (is_s3) {
        return SCENARIO_S3_PV;
    }

    /* S1 基准 */
    return SCENARIO_S1_BASELINE;
}

const char* scenario_name(scenario_type_t s)
{
    switch (s) {
        case SCENARIO_S1_BASELINE:      return "S1-基准负荷";
        case SCENARIO_S2_EV_CHARGING:   return "S2-充电桩接入";
        case SCENARIO_S3_PV:            return "S3-分布式光伏";
        case SCENARIO_S4_EV_PV:         return "S4-光充耦合";
        case SCENARIO_S5_EXTREME:       return "S5-极端场景";
        default:                        return "UNKNOWN";
    }
}

const char* scenario_recommendation(scenario_type_t s)
{
    switch (s) {
        case SCENARIO_S1_BASELINE:
            return "系统运行正常，继续监测。";
        case SCENARIO_S2_EV_CHARGING:
            return "建议：配置有源电力滤波器(APF)；实施有序充电策略，错峰调度充电桩功率。";
        case SCENARIO_S3_PV:
            return "建议：优化光伏逆变器无功调节策略；检查逆功率保护整定值。";
        case SCENARIO_S4_EV_PV:
            return "建议：配置储能系统平滑功率波动；协调光充出力时序，避免同时大功率运行。";
        case SCENARIO_S5_EXTREME:
            return "建议：立即启动负荷切除；投入备用容量；执行网络重构，隔离故障区域。";
        default:
            return "暂无建议。";
    }
}
