/**
 * @file main.c
 * @brief 嵌入式主入口
 */

#include "pq_common.h"

#if defined(PLATFORM_WINDOWS) || defined(_WIN32)
/* Windows模拟环境下由sim_main.c提供入口 */
int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    PQ_LOGI("PQ AI Terminal - Windows simulation build");
    PQ_LOGI("Please run pq_sim.exe for simulation mode.");
    return 0;
}
#else
/* 嵌入式RTOS/Linux入口 */
int main(void)
{
    PQ_LOGI("PQ AI Terminal - Embedded build");
    /* TODO: 初始化RTOS、启动任务 */
    while (1) {
        hal_sleep_ms(1000);
    }
    return 0;
}
#endif
