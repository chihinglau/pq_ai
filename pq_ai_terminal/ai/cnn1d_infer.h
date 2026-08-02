/**
 * @file cnn1d_infer.h
 * @brief 1D-CNN事件分类推理
 */

#ifndef CNN1D_INFER_H
#define CNN1D_INFER_H

#include "pq_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CNN_MAX_CLASSES 8

#define CNN_CLS_NORMAL    0
#define CNN_CLS_SAG       1
#define CNN_CLS_SWELL     2
#define CNN_CLS_HARMONIC  3
#define CNN_CLS_UNBALANCE 4
#define CNN_CLS_OVERLOAD  5
#define CNN_CLS_TRANSIENT 6

/**
 * @brief 初始化1D-CNN分类器
 * @param n_classes 类别数
 * @param pts_per_cycle 每周期采样点数
 * @return 0成功
 */
int cnn1d_init(int n_classes, int pts_per_cycle);

/**
 * @brief 对波形进行分类
 * @param wave 波形数据 [channels x pts_per_cycle]
 * @param n_channels 通道数
 * @param pts_per_cycle 每周期点数
 * @param n_cycles 周波数
 * @param probs 输出各类别概率数组 [n_classes]
 * @return 0成功
 */
int cnn1d_classify(const float *wave, int n_channels, int pts_per_cycle,
                   int n_cycles, float *probs);

/**
 * @brief 获取预测类别
 * @param probs 概率数组
 * @param n_classes 类别数
 * @param confidence 输出置信度
 * @return 类别索引
 */
int cnn1d_get_class(const float *probs, int n_classes, float *confidence);

#ifdef __cplusplus
}
#endif

#endif /* CNN1D_INFER_H */
