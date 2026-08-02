/**
 * @file wave_freeze.c
 * @brief 波形冻结与环形缓冲模块实现
 * @author PQ AI Terminal Team
 * @date 2026-08-02
 */

#include "wave_freeze.h"
#include <string.h>

int wave_freeze_init(wave_freeze_buffer_t *buf)
{
    if (buf == NULL) {
        return -1;
    }

    memset(buf, 0, sizeof(wave_freeze_buffer_t));
    buf->head = 0;
    buf->count = 0;
    buf->frozen = 0;
    buf->frozen_cycle = 0;

    return 0;
}

int wave_freeze_push(wave_freeze_buffer_t *buf, const ht7627s_wave_t *wave)
{
    wave_cycle_t *cycle;
    size_t sample_bytes;

    if (buf == NULL || wave == NULL) {
        return -1;
    }

    /* 冻结后不再写入 */
    if (buf->frozen) {
        return 0;
    }

    cycle = &buf->cycles[buf->head];

    /* 复制采样数据 */
    sample_bytes = sizeof(float) * PQ_N_CHANNELS * PQ_POINTS_PER_CYCLE_25600;
    memcpy(cycle->samples, wave->samples, sample_bytes);
    cycle->cycle_id = wave->cycle_id;
    cycle->timestamp_us = wave->timestamp_us;

    /* 更新环形缓冲指针 */
    buf->head = (buf->head + 1) % WAVE_FREEZE_MAX_CYCLES;
    if (buf->count < WAVE_FREEZE_MAX_CYCLES) {
        buf->count++;
    }

    return 0;
}

int wave_freeze_trigger(wave_freeze_buffer_t *buf, uint32_t cycle_id)
{
    if (buf == NULL) {
        return -1;
    }

    buf->frozen = 1;
    buf->frozen_cycle = cycle_id;

    return 0;
}

int wave_freeze_get_frozen(wave_freeze_buffer_t *buf, wave_cycle_t **cycles, uint16_t *n_cycles)
{
    if (buf == NULL || cycles == NULL || n_cycles == NULL) {
        return -1;
    }

    if (!buf->frozen) {
        *cycles = NULL;
        *n_cycles = 0;
        return -2;
    }

    /* 返回缓冲区中所有有效周波 */
    *cycles = buf->cycles;
    *n_cycles = buf->count;

    return 0;
}

int wave_freeze_reset(wave_freeze_buffer_t *buf)
{
    if (buf == NULL) {
        return -1;
    }

    buf->head = 0;
    buf->count = 0;
    buf->frozen = 0;
    buf->frozen_cycle = 0;

    return 0;
}
