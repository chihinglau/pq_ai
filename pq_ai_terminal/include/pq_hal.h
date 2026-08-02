/**
 * @file pq_hal.h
 * @brief 硬件抽象层接口，定义HT7627S寄存器和抽象操作
 * @author PQ AI Terminal Team
 * @date 2026-08-02
 */

#ifndef PQ_HAL_H
#define PQ_HAL_H

#include "pq_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief HT7627S配置结构体
 */
typedef struct {
    uint32_t sample_rate;      /**< 采样率：12800 或 25600 */
    uint16_t pts_per_cycle;    /**< 每周期点数：256 或 512 */
    uint16_t n_channels;       /**< 通道数：7 */
    uint32_t cycle_count;      /**< 当前周波计数 */
    uint64_t timestamp_us;     /**< 微秒时间戳 */
} ht7627s_cfg_t;

/**
 * @brief HT7627S寄存器数据（计算结果）
 */
typedef struct {
    float rms[PQ_N_CHANNELS];              /**< 7通道RMS */
    float thd_v;                           /**< 电压THD (%) */
    float thd_i;                           /**< 电流THD (%) */
    float harmonics[PQ_MAX_HARMONIC_ORDER]; /**< 2-31次谐波含有率(%) */
    float active_power;                    /**< 有功功率(kW) */
    float reactive_power;                  /**< 无功功率(kvar) */
    float apparent_power;                  /**< 视在功率(kVA) */
    float power_factor;                    /**< 功率因数 */
    float frequency;                       /**< 频率(Hz) */
    float phase_angle[PQ_N_CHANNELS];        /**< 相位角 */
} ht7627s_regs_t;

/**
 * @brief HT7627S波形数据
 */
typedef struct {
    float samples[PQ_N_CHANNELS][PQ_POINTS_PER_CYCLE_25600]; /**< 原始采样值 */
    uint16_t valid_samples;   /**< 实际有效点数 */
    uint32_t cycle_id;        /**< 周波序号 */
    uint64_t timestamp_us;    /**< 时间戳 */
} ht7627s_wave_t;

/**
 * @brief 初始化HT7627S
 * @param cfg 配置参数指针
 * @return 0成功，非0失败
 */
int hal_ht7627s_init(const ht7627s_cfg_t *cfg);

/**
 * @brief 读取HT7627S寄存器数据
 * @param regs 寄存器数据结构体指针
 * @return 0成功，非0失败
 */
int hal_ht7627s_read_regs(ht7627s_regs_t *regs);

/**
 * @brief 读取HT7627S波形数据
 * @param wave 波形数据结构体指针
 * @return 0成功，非0失败
 */
int hal_ht7627s_read_wave(ht7627s_wave_t *wave);

/**
 * @brief 获取当前微秒时间戳
 * @param t 时间戳输出指针
 * @return 0成功，非0失败
 */
int hal_ht7627s_get_time_us(uint64_t *t);

/**
 * @brief 毫秒级睡眠
 * @param ms 毫秒数
 */
void hal_sleep_ms(uint32_t ms);

/**
 * @brief 进入临界区
 */
void hal_critical_enter(void);

/**
 * @brief 退出临界区
 */
void hal_critical_exit(void);

#ifdef __cplusplus
}
#endif

#endif /* PQ_HAL_H */
