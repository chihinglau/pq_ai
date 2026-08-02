/**
 * @file feature_extract.h
 * @brief 特征工程模块头文件
 * @author PQ AI Terminal Team
 * @date 2026-08-02
 */

#ifndef FEATURE_EXTRACT_H
#define FEATURE_EXTRACT_H

#include "pq_common.h"
#include "pq_metrics.h"
#include "wave_freeze.h"

#ifdef __cplusplus
extern "C" {
#endif

#define N_STD_FEATURES   16
#define N_WAVE_FEATURES  11
#define N_TOTAL_FEATURES (N_STD_FEATURES + N_WAVE_FEATURES)

/**
 * @brief 特征向量结构体
 */
typedef struct {
    float features[N_TOTAL_FEATURES];
    char  names[N_TOTAL_FEATURES][32];
    uint16_t n_features;
} feature_vector_t;

/**
 * @brief 初始化特征工程模块
 * @return 0成功，非0失败
 */
int feature_extract_init(void);

/**
 * @brief 从波形和指标中提取特征向量
 * @param cycles 周波数组
 * @param n_cycles 周波数量
 * @param metrics PQ指标
 * @param out 输出特征向量
 * @return 0成功，非0失败
 */
int feature_extract_from_wave(const wave_cycle_t *cycles, int n_cycles, const pq_metrics_t *metrics, feature_vector_t *out);

#ifdef __cplusplus
}
#endif

#endif /* FEATURE_EXTRACT_H */
