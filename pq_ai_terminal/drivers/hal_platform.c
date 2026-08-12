/**
 * @file hal_platform.c
 * @brief 平台相关的通用HAL实现
 * 
 * 提供延时、临界区、时间获取等通用功能
 * 硬件相关的HT7627S操作在真实驱动中实现
 */

#include "pq_hal.h"
#include "pq_common.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifdef PLATFORM_WINDOWS
    #include <windows.h>
#else
    #include <sys/time.h>
    #include <poll.h>
    #include <pthread.h>
#endif

/* 静态状态 */
#ifdef PLATFORM_WINDOWS
static CRITICAL_SECTION s_crit;
static int s_crit_initialized = 0;
#else
static pthread_mutex_t s_crit = PTHREAD_MUTEX_INITIALIZER;
#endif

/**
 * @brief 初始化HT7627S（占位实现，真实硬件需对接驱动）
 */
int hal_ht7627s_init(const ht7627s_cfg_t *cfg)
{
    if (cfg == NULL) {
        return -1;
    }
    
#ifdef PLATFORM_WINDOWS
    if (!s_crit_initialized) {
        InitializeCriticalSection(&s_crit);
        s_crit_initialized = 1;
    }
#endif
    
    PQ_LOGI("hal_ht7627s_init: sample_rate=%u, pts_per_cycle=%u, n_channels=%u",
            cfg->sample_rate, cfg->pts_per_cycle, cfg->n_channels);
    return 0;
}

/**
 * @brief 读取HT7627S寄存器数据（占位实现）
 */
int hal_ht7627s_read_regs(ht7627s_regs_t *regs)
{
    if (regs == NULL) {
        return -1;
    }
    memset(regs, 0, sizeof(ht7627s_regs_t));
    return 0;
}

/**
 * @brief 读取HT7627S波形数据（占位实现）
 */
int hal_ht7627s_read_wave(ht7627s_wave_t *wave)
{
    if (wave == NULL) {
        return -1;
    }
    memset(wave, 0, sizeof(ht7627s_wave_t));
    return 0;
}

/**
 * @brief 获取当前微秒时间戳
 */
int hal_ht7627s_get_time_us(uint64_t *t)
{
    if (t == NULL) {
        return -1;
    }
    
#ifdef PLATFORM_WINDOWS
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    *t = (uint64_t)now.QuadPart * 1000000ULL / (uint64_t)freq.QuadPart;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    *t = (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
#endif
    
    return 0;
}

/**
 * @brief 毫秒级睡眠
 */
void hal_sleep_ms(uint32_t ms)
{
#ifdef PLATFORM_WINDOWS
    Sleep(ms);
#else
    poll(NULL, 0, (int)ms);
#endif
}

/**
 * @brief 进入临界区
 */
void hal_critical_enter(void)
{
#ifdef PLATFORM_WINDOWS
    EnterCriticalSection(&s_crit);
#else
    pthread_mutex_lock(&s_crit);
#endif
}

/**
 * @brief 退出临界区
 */
void hal_critical_exit(void)
{
#ifdef PLATFORM_WINDOWS
    LeaveCriticalSection(&s_crit);
#else
    pthread_mutex_unlock(&s_crit);
#endif
}
