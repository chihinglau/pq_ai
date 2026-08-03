/**
 * @file ai_rpc.c
 * @brief AI 推理 RPC 客户端实现（主机侧）
 *
 * 通过 USB ECM 向 RK3576 算力模组发送特征向量，获取 AI 推理结果。
 * 算力模组不可达时，自动降级为本地 Stub 推理。
 *
 * @author PQ AI Terminal Team
 * @date 2026-08-02
 */

#include "ai_rpc.h"
#include "usb_ecm.h"
#include "iforest_infer.h"
#include "ae_infer.h"
#include "cnn1d_infer.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ==================== 模块状态 ==================== */
static usb_ecm_t        g_ecm;
static int              g_rpc_init = 0;
static int              g_module_online = 0;

/* 本地 fallback 模型（仅当算力模组不可达时使用） */
static iforest_model_t  g_local_if;
static int              g_local_loaded = 0;

/* 超时配置（ms） */
#define AI_RPC_TIMEOUT_MS   2000

/* ==================== 构建 JSON 请求 ==================== */
static int build_request(char *buf, int buf_size,
                         const feature_vector_t *feat,
                         const pq_metrics_t *metrics)
{
    int i, offset;

    offset = snprintf(buf, (size_t)buf_size,
        "{\"cmd\":\"infer\",\"features\":[");

    for (i = 0; i < feat->n_features && offset < buf_size - 20; i++) {
        offset += snprintf(buf + offset, (size_t)(buf_size - offset),
                           "%.4f%s", feat->features[i],
                           (i < feat->n_features - 1) ? "," : "");
    }

    offset += snprintf(buf + offset, (size_t)(buf_size - offset),
        "],\"vthd\":%.3f,\"ithd\":%.3f}",
        metrics->voltage_thd.value,
        metrics->current_thd.value);

    return (offset > 0 && offset < buf_size) ? 0 : -1;
}

/* ==================== 解析 JSON 应答 ==================== */
static int parse_response(const char *resp, ai_result_t *result)
{
    const char *p;

    memset(result, 0, sizeof(*result));

    /* 解析 "if":<float> */
    p = strstr(resp, "\"if\"");
    if (!p) p = strstr(resp, "if");
    if (p) {
        p = strchr(p, ':');
        if (p) { p++; result->if_score = strtof(p, NULL); }
    }

    /* 解析 "ae":<float> */
    p = strstr(resp, "\"ae\"");
    if (!p) p = strstr(resp, "ae");
    if (p) {
        p = strchr(p, ':');
        if (p) { p++; result->ae_score = strtof(p, NULL); }
    }

    /* 解析 "cls":<int> */
    p = strstr(resp, "\"cls\"");
    if (!p) p = strstr(resp, "cls");
    if (p) {
        p = strchr(p, ':');
        if (p) { p++; result->cnn_class = (int)strtol(p, NULL, 10); }
    }

    /* 解析 "conf":<float> */
    p = strstr(resp, "\"conf\"");
    if (!p) p = strstr(resp, "conf");
    if (p) {
        p = strchr(p, ':');
        if (p) { p++; result->cnn_confidence = strtof(p, NULL); }
    }

    /* 解析 "lat":<int> */
    p = strstr(resp, "\"lat\"");
    if (!p) p = strstr(resp, "lat");
    if (p) {
        p = strchr(p, ':');
        if (p) { p++; result->latency_ms = (int)strtol(p, NULL, 10); }
    }

    return 0;
}

/* ==================== 本地 fallback 推理 ==================== */
static void local_infer(const feature_vector_t *feat, ai_result_t *result)
{
    float probs[CNN_MAX_CLASSES];

    if (!g_local_loaded) {
        iforest_load_model(&g_local_if, NULL);
        ae_init(IF_N_FEATURES, 8);
        cnn1d_init(7, 256);
        g_local_loaded = 1;
    }

    result->if_score = iforest_score(&g_local_if, feat->features);
    result->ae_score = ae_anomaly_score(feat->features);
    cnn1d_classify(feat->features, 1, IF_N_FEATURES, 1, probs);
    result->cnn_class = cnn1d_get_class(probs, 7, &result->cnn_confidence);
    result->latency_ms = 0;
    result->module_available = 0;
}

/* ==================== 公开接口 ==================== */
int ai_rpc_init(const char *module_ip, int port)
{
    if (g_rpc_init) return 0;

    usb_ecm_init(&g_ecm,
                 module_ip ? module_ip : USB_ECM_SIM_IP,
                 (port > 0) ? port : USB_ECM_DEFAULT_PORT);
    g_rpc_init = 1;
    g_module_online = 0;
    PQ_LOGI("ai_rpc: initialized (target=%s:%d)", g_ecm.host_ip, g_ecm.port);
    return 0;
}

int ai_rpc_infer(const feature_vector_t *feat,
                 const pq_metrics_t *metrics,
                 ai_result_t *result)
{
    char request[USB_ECM_MAX_PACKET];
    char response[512];
    int  ret;

    if (!g_rpc_init || feat == NULL || metrics == NULL || result == NULL) {
        return -1;
    }

    /* 构建请求 */
    if (build_request(request, sizeof(request), feat, metrics) != 0) {
        PQ_LOGE("ai_rpc: build_request failed");
        local_infer(feat, result);
        return 0;
    }

    /* 通过 USB ECM 发送请求并等待应答 */
    ret = usb_ecm_request(&g_ecm, request, response, sizeof(response),
                           AI_RPC_TIMEOUT_MS);

    if (ret == 0) {
        /* 算力模组响应成功 */
        parse_response(response, result);
        result->module_available = 1;
        if (!g_module_online) {
            PQ_LOGI("ai_rpc: compute module ONLINE (RK3576 via USB ECM)");
            g_module_online = 1;
        }
    } else {
        /* 算力模组不可达，降级为本地推理 */
        if (g_module_online) {
            PQ_LOGW("ai_rpc: compute module offline, fallback to local stub");
            g_module_online = 0;
        }
        usb_ecm_disconnect(&g_ecm);
        local_infer(feat, result);
    }

    return 0;
}

int ai_rpc_module_online(void)
{
    return g_module_online;
}

void ai_rpc_deinit(void)
{
    if (!g_rpc_init) return;
    usb_ecm_disconnect(&g_ecm);
    g_rpc_init = 0;
    g_module_online = 0;
    PQ_LOGI("ai_rpc: deinitialized");
}
