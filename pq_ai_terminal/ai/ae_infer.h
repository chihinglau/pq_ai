/**
 * @file ae_infer.h
 * @brief 自编码器异常检测推理
 */

#ifndef AE_INFER_H
#define AE_INFER_H

#include "pq_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AE_MAX_DIM 64

/**
 * @brief 初始化自编码器
 * @param input_dim 输入维度
 * @param bottleneck_dim 瓶颈维度
 * @return 0成功
 */
int ae_init(int input_dim, int bottleneck_dim);

/**
 * @brief 编码
 * @param x 输入向量
 * @param z 输出编码向量
 * @return 0成功
 */
int ae_encode(const float *x, float *z);

/**
 * @brief 解码
 * @param z 编码向量
 * @param x_hat 输出重构向量
 * @return 0成功
 */
int ae_decode(const float *z, float *x_hat);

/**
 * @brief 计算重构误差 (MSE)
 * @param x 原始向量
 * @param x_hat 重构向量
 * @param n 维度
 * @return MSE值
 */
float ae_reconstruction_error(const float *x, const float *x_hat, int n);

/**
 * @brief 端到端异常得分
 * @param x 输入向量
 * @return 异常得分 (MSE)
 */
float ae_anomaly_score(const float *x);

#ifdef __cplusplus
}
#endif

#endif /* AE_INFER_H */
