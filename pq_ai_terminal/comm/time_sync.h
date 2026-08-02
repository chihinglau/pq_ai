/**
 * @file time_sync.h
 * @brief 时间同步接口
 */

#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include "pq_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化时间同步
 * @param ntp_server NTP服务器地址
 * @return 0成功
 */
int time_sync_init(const char *ntp_server);

/**
 * @brief 获取Unix时间戳（毫秒）
 * @return 毫秒级时间戳
 */
uint64_t time_sync_get_unix_ms(void);

/**
 * @brief 执行一次NTP同步
 * @return 0成功
 */
int time_sync_ntp_sync(void);

#ifdef __cplusplus
}
#endif

#endif /* TIME_SYNC_H */
