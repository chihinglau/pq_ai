/**
 * @file proto_mqtt.h
 * @brief MQTT客户端接口
 */

#ifndef PROTO_MQTT_H
#define PROTO_MQTT_H

#include "pq_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化MQTT客户端
 * @param broker_host Broker地址
 * @param port 端口
 * @param client_id 客户端ID
 * @return 0成功
 */
int mqtt_init(const char *broker_host, int port, const char *client_id);

/**
 * @brief 连接Broker
 * @return 0成功
 */
int mqtt_connect(void);

/**
 * @brief 发布消息
 * @param topic 主题
 * @param payload 载荷
 * @param qos QoS等级
 * @return 0成功
 */
int mqtt_publish(const char *topic, const char *payload, int qos);

/**
 * @brief 订阅主题
 * @param topic 主题
 * @return 0成功
 */
int mqtt_subscribe(const char *topic);

/**
 * @brief 断开连接
 */
void mqtt_disconnect(void);

#ifdef __cplusplus
}
#endif

#endif /* PROTO_MQTT_H */
