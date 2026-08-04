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

/* 启用 POSIX 扩展（clock_gettime 等），需在所有 include 之前定义 */
#ifndef PLATFORM_WINDOWS
#define _POSIX_C_SOURCE 200112L
#endif

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
static int              g_infer_count = 0;       /* 累计推理次数 */
static int              g_fallback_count = 0;    /* 累计降级次数 */

/* 本地 fallback 模型（仅当算力模组不可达时使用） */
static iforest_model_t  g_local_if;
static int              g_local_loaded = 0;

/* 超时配置（ms） */
#define AI_RPC_TIMEOUT_MS   2000

/* ==================== 跨平台毫秒计时器 ==================== */
static uint64_t ai_now_ms(void)
{
#if defined(PLATFORM_WINDOWS) || defined(_WIN32)
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000U + (uint64_t)ts.tv_nsec / 1000000U;
#endif
}

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
    g_infer_count = 0;
    g_fallback_count = 0;
    PQ_LOGI("ai_rpc: initialized (target=%s:%d, timeout=%d ms)",
            g_ecm.host_ip, g_ecm.port, AI_RPC_TIMEOUT_MS);
    return 0;
}

int ai_rpc_infer(const feature_vector_t *feat,
                 const pq_metrics_t *metrics,
                 ai_result_t *result)
{
    char request[USB_ECM_MAX_PACKET];
    char response[512];
    int  ret;
    uint64_t t0, t1;

    if (!g_rpc_init || feat == NULL || metrics == NULL || result == NULL) {
        PQ_LOGE("ai_rpc: invalid args (init=%d, feat=%p, metrics=%p, result=%p)",
                g_rpc_init, (const void *)feat, (const void *)metrics,
                (const void *)result);
        return -1;
    }

    g_infer_count++;
    t0 = ai_now_ms();

    /* 构建请求 */
    if (build_request(request, sizeof(request), feat, metrics) != 0) {
        PQ_LOGE("ai_rpc: build_request failed (n_features=%d)", feat->n_features);
        local_infer(feat, result);
        g_fallback_count++;
        t1 = ai_now_ms();
        PQ_LOGW("ai_rpc: fallback to local (reason=build_request, latency=%llu ms, "
                "total_fallback=%d/%d)",
                (unsigned long long)(t1 - t0), g_fallback_count, g_infer_count);
        return 0;
    }

    PQ_LOGI("ai_rpc: infer #%d, request %d B (n_feat=%d, vthd=%.2f, ithd=%.2f)",
            g_infer_count, (int)strlen(request), feat->n_features,
            metrics->voltage_thd.value, metrics->current_thd.value);

    /* 通过 USB ECM 发送请求并等待应答 */
    ret = usb_ecm_request(&g_ecm, request, response, sizeof(response),
                           AI_RPC_TIMEOUT_MS);

    if (ret == 0) {
        /* 算力模组响应成功 */
        parse_response(response, result);
        result->module_available = 1;
        if (!g_module_online) {
            PQ_LOGI("ai_rpc: compute module ONLINE (RK3576 via USB ECM) "
                    "[recovered after %d fallbacks]", g_fallback_count);
            g_module_online = 1;
        }
        t1 = ai_now_ms();
        result->latency_ms = (int)(t1 - t0);
        PQ_LOGI("ai_rpc: infer OK #%d (if=%.4f, ae=%.2f, cls=%d, conf=%.4f, "
                "latency=%d ms)",
                g_infer_count, result->if_score, result->ae_score,
                result->cnn_class, result->cnn_confidence, result->latency_ms);
    } else {
        /* 算力模组不可达，降级为本地推理 */
        g_fallback_count++;
        if (g_module_online) {
            PQ_LOGW("ai_rpc: compute module OFFLINE, fallback to local stub "
                    "(reason=usb_ecm_request ret=%d, total_fallback=%d/%d)",
                    ret, g_fallback_count, g_infer_count);
            g_module_online = 0;
        } else {
            /* 已离线状态，避免每周期刷屏，仅每 10 次打印一次 */
            if (g_fallback_count % 10 == 0) {
                PQ_LOGW("ai_rpc: still offline, %d consecutive fallbacks "
                        "(total_infer=%d)", g_fallback_count, g_infer_count);
            }
        }
        usb_ecm_disconnect(&g_ecm);
        local_infer(feat, result);
        t1 = ai_now_ms();
        PQ_LOGW("ai_rpc: local fallback #%d done (latency=%llu ms)",
                g_infer_count, (unsigned long long)(t1 - t0));
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
    PQ_LOGI("ai_rpc: deinit (total_infer=%d, total_fallback=%d, success_rate=%.1f%%)",
            g_infer_count, g_fallback_count,
            g_infer_count > 0 ?
            100.0f * (g_infer_count - g_fallback_count) / g_infer_count : 0.0f);
    usb_ecm_disconnect(&g_ecm);
    g_rpc_init = 0;
    g_module_online = 0;
}
