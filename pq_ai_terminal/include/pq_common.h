/**
 * @file pq_common.h
 * @brief 公共头文件，定义基本类型、平台宏、常量和调试宏
 * @author PQ AI Terminal Team
 * @date 2026-08-02
 */

#ifndef PQ_COMMON_H
#define PQ_COMMON_H

#include <stdint.h>
#include <stdio.h>
#include <time.h>

#ifdef PLATFORM_WINDOWS
    #include <windows.h>
#endif

/* 基本类型别名 —— stdint.h 已提供整数类型 */
typedef float    float32_t;

/* 平台宏已在CMakeLists.txt中通过add_definitions定义 */

/* 关键常量 */
#define PQ_SAMPLE_RATE_12800        12800U
#define PQ_SAMPLE_RATE_25600        25600U
#define PQ_POINTS_PER_CYCLE_12800   256U
#define PQ_POINTS_PER_CYCLE_25600   512U
#define PQ_NOMINAL_FREQ             50.0f
#define PQ_NOMINAL_VOLTAGE          220.0f
#define PQ_N_CHANNELS               7U
#define PQ_MAX_HARMONIC_ORDER       31U
#define PI                          3.14159265358979323846f

/* 函数属性宏 */
#ifdef _MSC_VER
    #define INLINE      __inline
    #define LIKELY(x)   (x)
    #define UNLIKELY(x) (x)
#else
    #define INLINE      inline
    #define LIKELY(x)   __builtin_expect(!!(x), 1)
    #define UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif

/* 调试宏 */
#define PQ_LOGI(fmt, ...) \
    printf("[PQ][INFO] %s:%d " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define PQ_LOGW(fmt, ...) \
    printf("[PQ][WARN] %s:%d " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define PQ_LOGE(fmt, ...) \
    printf("[PQ][ERR ] %s:%d " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#endif /* PQ_COMMON_H */
