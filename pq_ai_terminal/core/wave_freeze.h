/**
 * @file wave_freeze.h
 * @brief 波形冻结与环形缓冲模块头文件
 * @author PQ AI Terminal Team
 * @date 2026-08-02
 */

#ifndef WAVE_FREEZE_H
#define WAVE_FREEZE_H

#include "pq_common.h"
#include "pq_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WAVE_FREEZE_PRE_CYCLES   10
#define WAVE_FREEZE_POST_CYCLES  20
#define WAVE_FREEZE_MAX_CYCLES   (WAVE_FREEZE_PRE_CYCLES + WAVE_FREEZE_POST_CYCLES)

/**
 * @brief 单周波波形数据
 */
typedef struct {
    float    samples[PQ_N_CHANNELS][PQ_POINTS_PER_CYCLE_25600];
    uint32_t cycle_id;
    uint64_t timestamp_us;
} wave_cycle_t;

/**
 * @brief 波形冻结环形缓冲区
 */
typedef struct {
    wave_cycle_t cycles[WAVE_FREEZE_MAX_CYCLES];
    uint16_t     head;           /**< 写入位置 */
    uint16_t     count;          /**< 有效周波数 */
    uint8_t      frozen;         /**< 是否已冻结 */
    uint32_t     frozen_cycle;   /**< 冻结时刻周波号 */
} wave_freeze_buffer_t;

/**
 * @brief 初始化波形冻结缓冲区
 * @param buf 缓冲区指针
 * @return 0成功，非0失败
 */
int wave_freeze_init(wave_freeze_buffer_t *buf);

/**
 * @brief 将新周波推入缓冲
 * @param buf 缓冲区指针
 * @param wave HT7627S波形数据
 * @return 0成功，非0失败
 */
int wave_freeze_push(wave_freeze_buffer_t *buf, const ht7627s_wave_t *wave);

/**
 * @brief 冻结当前缓冲
 * @param buf 缓冲区指针
 * @param cycle_id 触发冻结的周波号
 * @return 0成功，非0失败
 */
int wave_freeze_trigger(wave_freeze_buffer_t *buf, uint32_t cycle_id);

/**
 * @brief 获取冻结的波形
 * @param buf 缓冲区指针
 * @param cycles 输出周波数组指针
 * @param n_cycles 输出周波数量
 * @return 0成功，非0失败
 */
int wave_freeze_get_frozen(wave_freeze_buffer_t *buf, wave_cycle_t **cycles, uint16_t *n_cycles);

/**
 * @brief 重置波形冻结缓冲区
 * @param buf 缓冲区指针
 * @return 0成功，非0失败
 */
int wave_freeze_reset(wave_freeze_buffer_t *buf);

#ifdef __cplusplus
}
#endif

#endif /* WAVE_FREEZE_H */
