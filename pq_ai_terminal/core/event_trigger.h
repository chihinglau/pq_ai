/**
 * @file event_trigger.h
 * @brief 事件触发引擎头文件
 * @author PQ AI Terminal Team
 * @date 2026-08-02
 */

#ifndef EVENT_TRIGGER_H
#define EVENT_TRIGGER_H

#include "pq_common.h"
#include "pq_metrics.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EVENT_TYPE_VOLTAGE_SAG    1
#define EVENT_TYPE_VOLTAGE_SWELL  2
#define EVENT_TYPE_HARMONIC       3
#define EVENT_TYPE_UNBALANCE      4
#define EVENT_TYPE_OVERLOAD       5
#define EVENT_TYPE_FREQ_DEV       6
#define EVENT_TYPE_SCENARIO_CHG   7
#define EVENT_TYPE_UNKNOWN        0

/**
 * @brief PQ事件结构体
 */
typedef struct {
    uint32_t id;
    uint8_t  type;
    uint32_t start_ts;
    uint32_t end_ts;
    float    severity;
    uint8_t  phase;
    pq_metric_t trigger_metric;
    char     description[128];
} pq_event_t;

/**
 * @brief 初始化事件触发引擎
 * @return 0成功，非0失败
 */
int event_trigger_init(void);

/**
 * @brief 检查是否触发事件
 * @param metrics 当前PQ指标
 * @param event_out 输出事件结构体
 * @return 0未触发，1触发事件，非0错误
 */
int event_trigger_check(const pq_metrics_t *metrics, pq_event_t *event_out);

/**
 * @brief 事件类型转字符串
 * @param type 事件类型码
 * @return 类型字符串
 */
const char* event_type_str(uint8_t type);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_TRIGGER_H */
