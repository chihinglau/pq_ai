/**
 * @file pq_metrics.h
 * @brief 标准电能质量指标计算模块头文件
 * @author PQ AI Terminal Team
 * @date 2026-08-02
 */

#ifndef PQ_METRICS_H
#define PQ_METRICS_H

#include "pq_common.h"
#include "pq_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 单个电能质量指标结构体
 */
typedef struct {
    char     name[32];
    float    value;
    float    limit;
    int      status;        /**< 0=正常, 1=预警, 2=超标 */
    uint32_t timestamp;
    uint16_t pts_per_cycle;
    uint8_t  phase;
    float    confidence;
} pq_metric_t;

/**
 * @brief 全部电能质量指标集合
 */
typedef struct {
    pq_metric_t voltage_deviation;   /**< 电压偏差 % */
    pq_metric_t voltage_thd;         /**< 电压THD % */
    pq_metric_t current_thd;         /**< 电流THD % */
    pq_metric_t voltage_unbalance;   /**< 电压不平衡度 % */
    pq_metric_t frequency_deviation; /**< 频率偏差 Hz */
    pq_metric_t transformer_load;    /**< 变压器负载率 % */
    pq_metric_t line_load;           /**< 线路负载率 % */
    pq_metric_t power_factor;        /**< 功率因数 */
    pq_metric_t active_power;        /**< 有功功率 kW */
    pq_metric_t reactive_power;      /**< 无功功率 kvar */
    pq_metric_t apparent_power;      /**< 视在功率 kVA */
    pq_metric_t harmonic_3rd;        /**< 3次谐波含有率 % */
} pq_metrics_t;

/**
 * @brief 初始化PQ指标计算模块
 * @param v_nom 额定电压(V)
 * @param f_nom 额定频率(Hz)
 * @return 0成功，非0失败
 */
int pq_metrics_init(float v_nom, float f_nom);

/**
 * @brief 设置评估限值
 * @param voltage_deviation 电压偏差限值(%)
 * @param voltage_thd 电压THD限值(%)
 * @param current_thd 电流THD限值(%)
 * @param voltage_unbalance 不平衡度限值(%)
 * @param frequency_deviation 频率偏差限值(Hz)
 * @param transformer_load 变压器负载限值(%)
 * @param line_load 线路负载限值(%)
 * @param power_factor 功率因数下限
 */
void pq_metrics_set_limits(float voltage_deviation, float voltage_thd, float current_thd,
                           float voltage_unbalance, float frequency_deviation,
                           float transformer_load, float line_load, float power_factor);

/**
 * @brief 计算所有电能质量指标
 * @param regs HT7627S寄存器数据
 * @param wave HT7627S波形数据
 * @param out 输出指标结构体
 * @return 0成功，非0失败
 */
int pq_metrics_calculate(const ht7627s_regs_t *regs, const ht7627s_wave_t *wave, pq_metrics_t *out);

/**
 * @brief 根据国标限值评估各指标状态
 * @param metrics 指标结构体指针
 * @return 0成功，非0失败
 */
int pq_metrics_evaluate(pq_metrics_t *metrics);

/**
 * @brief 状态码转字符串
 * @param status 状态码(0/1/2)
 * @return 状态字符串
 */
const char* pq_metric_status_str(int status);

#ifdef __cplusplus
}
#endif

#endif /* PQ_METRICS_H */
