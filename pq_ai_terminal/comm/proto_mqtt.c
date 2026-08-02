/**
 * @file proto_mqtt.c
 * @brief MQTT客户端实现 (Windows模拟)
 */

#include "proto_mqtt.h"
#include <stdio.h>
#include <string.h>

static struct {
    char host[64];
    int port;
    char client_id[32];
    int connected;
} g_mqtt = {0};

int mqtt_init(const char *broker_host, int port, const char *client_id)
{
    if (broker_host) {
        strncpy(g_mqtt.host, broker_host, sizeof(g_mqtt.host) - 1);
    }
    g_mqtt.port = port;
    if (client_id) {
        strncpy(g_mqtt.client_id, client_id, sizeof(g_mqtt.client_id) - 1);
    }
    g_mqtt.connected = 0;
    PQ_LOGI("MQTT init: host=%s port=%d client=%s", g_mqtt.host, port, g_mqtt.client_id);
    return 0;
}

int mqtt_connect(void)
{
    PQ_LOGI("MQTT connect: %s:%d", g_mqtt.host, g_mqtt.port);
    g_mqtt.connected = 1;
    return 0;
}

int mqtt_publish(const char *topic, const char *payload, int qos)
{
    (void)qos;
    if (!g_mqtt.connected) {
        PQ_LOGW("MQTT not connected, print to console instead");
    }
    printf("[MQTT] topic=%s payload=%s\n", topic ? topic : "null", payload ? payload : "null");
    return 0;
}

int mqtt_subscribe(const char *topic)
{
    PQ_LOGI("MQTT subscribe: %s", topic ? topic : "null");
    return 0;
}

void mqtt_disconnect(void)
{
    PQ_LOGI("MQTT disconnect");
    g_mqtt.connected = 0;
}
