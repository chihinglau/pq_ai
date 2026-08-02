/**
 * @file iforest_infer.h
 * @brief 孤立森林异常检测推理
 */

#ifndef IFOREST_INFER_H
#define IFOREST_INFER_H

#include "pq_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IF_MAX_TREES     100
#define IF_MAX_DEPTH     8
#define IF_N_FEATURES    27

/**
 * @brief 孤立森林树节点
 */
typedef struct {
    int   feat;
    float thr;
    int   left;
    int   right;
} if_node_t;

/**
 * @brief 孤立森林模型
 */
typedef struct {
    if_node_t nodes[IF_MAX_TREES * (1 << IF_MAX_DEPTH)];
    int n_trees;
    int n_features;
} iforest_model_t;

/**
 * @brief 加载孤立森林模型
 * @param model 模型结构体
 * @param path 模型文件路径
 * @return 0成功
 */
int iforest_load_model(iforest_model_t *model, const char *path);

/**
 * @brief 计算异常得分
 * @param model 模型
 * @param x 特征向量 (n_features维)
 * @return 异常得分 [0,1]，越接近1越异常
 */
float iforest_score(const iforest_model_t *model, const float *x);

/**
 * @brief 判断是否为异常
 * @param model 模型
 * @param x 特征向量
 * @param threshold 阈值 (默认0.6)
 * @return 1异常，0正常
 */
int iforest_is_anomaly(const iforest_model_t *model, const float *x, float threshold);

#ifdef __cplusplus
}
#endif

#endif /* IFOREST_INFER_H */
