/**
 * @file sqlite_wrapper.h
 * @brief 数据持久化封装
 */

#ifndef SQLITE_WRAPPER_H
#define SQLITE_WRAPPER_H

#include "pq_common.h"
#include "pq_metrics.h"
#include "event_trigger.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化数据库
 * @param path 文件路径前缀
 * @return 0成功
 */
int db_init(const char *path);

/**
 * @brief 保存PQ指标
 * @param metrics PQ指标
 * @return 0成功
 */
int db_save_metrics(const pq_metrics_t *metrics);

/**
 * @brief 保存事件
 * @param event 事件
 * @return 0成功
 */
int db_save_event(const pq_event_t *event);

/**
 * @brief 关闭数据库
 * @return 0成功
 */
int db_close(void);

#ifdef __cplusplus
}
#endif

#endif /* SQLITE_WRAPPER_H */
