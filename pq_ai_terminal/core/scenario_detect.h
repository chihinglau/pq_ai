/**
 * @file scenario_detect.h
 * @brief 场景识别模块头文件
 * @author PQ AI Terminal Team
 * @date 2026-08-02
 */

#ifndef SCENARIO_DETECT_H
#define SCENARIO_DETECT_H

#include "pq_common.h"
#include "pq_metrics.h"
#include "feature_extract.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 场景类型枚举
 */
typedef enum {
    SCENARIO_S1_BASELINE = 1,
    SCENARIO_S2_EV_CHARGING = 2,
    SCENARIO_S3_PV = 3,
    SCENARIO_S4_EV_PV = 4,
    SCENARIO_S5_EXTREME = 5,
    SCENARIO_UNKNOWN = 0
} scenario_type_t;

/**
 * @brief 初始化场景识别模块
 * @return 0成功，非0失败
 */
int scenario_detect_init(void);

/**
 * @brief 根据指标和特征向量识别当前场景
 * @param metrics PQ指标
 * @param feat 特征向量
 * @return 场景类型
 */
scenario_type_t scenario_detect_classify(const pq_metrics_t *metrics, const feature_vector_t *feat);

/**
 * @brief 场景类型转名称字符串
 * @param s 场景类型
 * @return 场景名称
 */
const char* scenario_name(scenario_type_t s);

/**
 * @brief 场景治理建议
 * @param s 场景类型
 * @return 建议字符串
 */
const char* scenario_recommendation(scenario_type_t s);

#ifdef __cplusplus
}
#endif

#endif /* SCENARIO_DETECT_H */
