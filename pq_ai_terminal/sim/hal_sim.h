/**
 * @file hal_sim.h
 * @brief Windows仿真环境下HAL接口声明
 * @author PQ AI Terminal Team
 * @date 2026-08-02
 */

#ifndef HAL_SIM_H
#define HAL_SIM_H

#include "pq_common.h"
#include "pq_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 场景ID宏定义 */
#define SCEN_S1  0x5331
#define SCEN_S2  0x5332
#define SCEN_S3  0x5333
#define SCEN_S4  0x5334
#define SCEN_S5  0x5335

/**
 * @brief 全局仿真场景标识
 * @details SCEN_S1 ~ SCEN_S5
 */
extern uint16_t g_sim_scenario;

/**
 * @brief 仿真场景参数结构体
 */
typedef struct {
    float voltage_offset;           /**< 电压偏移百分比 */
    float unbalance_b;              /**< B相不平衡相位偏移 */
    float unbalance_c;              /**< C相不平衡相位偏移 */
    float harmonic_v[PQ_MAX_HARMONIC_ORDER]; /**< 电压谐波幅值(%基波) */
    float harmonic_i[PQ_MAX_HARMONIC_ORDER]; /**< 电流谐波幅值(%基波) */
    float noise_snr_db;             /**< 噪声信噪比(dB) */
    float load_active_kw;           /**< 负荷有功功率(kW) */
    float load_pf;                  /**< 负荷功率因数 */
} sim_scenario_params_t;

/**
 * @brief 获取当前场景参数
 * @return 场景参数指针
 */
const sim_scenario_params_t* sim_get_scenario_params(uint16_t scenario);

/**
 * @brief 设置仿真场景
 * @param scenario 场景字符串，如 "S1", "S2" 等
 */
void hal_set_sim_scenario(const char *scenario);

/**
 * @brief 生成波形数据
 * @param wave 波形数据输出
 * @param params 场景参数
 * @param sample_rate 采样率
 */
void sim_generate_wave(ht7627s_wave_t *wave, const sim_scenario_params_t *params, uint32_t sample_rate);

/**
 * @brief 从波形计算寄存器值
 * @param regs 寄存器输出
 * @param wave 输入波形
 */
void sim_calc_regs_from_wave(ht7627s_regs_t *regs, const ht7627s_wave_t *wave);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SIM_H */
