/**
 * @file test_usb_ecm_ai_rpc.c
 * @brief USB ECM 通信与 AI 推理 RPC 模块单元测试
 *
 * 测试覆盖范围：
 *   1. USB ECM 传输层（usb_ecm.c）
 *      - 正常流程：init → connect → send → recv → disconnect
 *      - 异常流程：连接不存在的 IP/端口、超时、空参数校验
 *   2. AI RPC 客户端（ai_rpc.c）
 *      - 正常流程：算力模组在线，推理结果返回正确
 *      - 降级场景 1：算力模组未启动，自动 fallback 到本地推理
 *      - 降级场景 2：算力模组运行中突然停止，后续推理自动降级
 *      - 恢复场景：算力模组恢复后，AI RPC 自动重连并恢复 ONLINE
 *   3. 算力模组仿真器（compute_module_sim.c）
 *      - 启动/停止、端口监听验证、并发推理请求
 *
 * 使用方法：
 *   mingw32-make test        # Windows
 *   make test                # Linux
 *   ./test_usb_ecm_ai_rpc    # 运行测试
 *
 * @author PQ AI Terminal Team
 * @date 2026-08-03
 */

/* 启用 POSIX 扩展（Linux 下 clock_gettime 等） */
#ifndef PLATFORM_WINDOWS
#define _POSIX_C_SOURCE 200112L
#endif

#include "pq_common.h"
#include "usb_ecm.h"
#include "ai_rpc.h"
#include "compute_module_sim.h"
#include "pq_metrics.h"
#include "feature_extract.h"
#include "iforest_infer.h"
#include "ae_infer.h"
#include "cnn1d_infer.h"

#include <string.h>
#include <stdlib.h>

#if defined(PLATFORM_WINDOWS) || defined(_WIN32)
    #include <winsock2.h>
    #include <windows.h>
    #define PQ_SLEEP_MS(ms)  Sleep(ms)
#else
    #include <unistd.h>
    #include <time.h>
    #define PQ_SLEEP_MS(ms)  usleep((ms) * 1000)
#endif

/* ==================== 测试框架 ==================== */

static int g_tests_run    = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("  [FAIL] %s (line %d)\n", msg, __LINE__); \
            return -1; \
        } \
    } while (0)

#define TEST_ASSERT_EQ(a, b, msg) \
    do { \
        if ((a) != (b)) { \
            printf("  [FAIL] %s: expected %d, got %d (line %d)\n", \
                    msg, (int)(b), (int)(a), __LINE__); \
            return -1; \
        } \
    } while (0)

#define RUN_TEST(test_func) \
    do { \
        printf("=== RUN  %s\n", #test_func); \
        g_tests_run++; \
        if (test_func() == 0) { \
            g_tests_passed++; \
            printf("=== PASS %s\n\n", #test_func); \
        } else { \
            g_tests_failed++; \
            printf("=== FAIL %s\n\n", #test_func); \
        } \
    } while (0)

/* 测试专用端口（避免与 config.ini 默认 9090 冲突） */
#define TEST_PORT_BASE    9200
#define TEST_PORT_NORMAL  TEST_PORT_BASE
#define TEST_PORT_OFFLINE (TEST_PORT_BASE + 1)
#define TEST_PORT_RECOVERY (TEST_PORT_BASE + 2)

/* ==================== 辅助函数 ==================== */

/**
 * @brief 构造测试用特征向量
 */
static void build_test_features(feature_vector_t *feat)
{
    int i;
    memset(feat, 0, sizeof(*feat));
    for (i = 0; i < IF_N_FEATURES && i < N_TOTAL_FEATURES; i++) {
        feat->features[i] = (float)(i + 1) * 0.1f;
        snprintf(feat->names[i], sizeof(feat->names[i]), "feat_%d", i);
    }
    feat->n_features = IF_N_FEATURES;
}

/**
 * @brief 构造测试用 PQ 指标
 */
static void build_test_metrics(pq_metrics_t *m)
{
    memset(m, 0, sizeof(*m));
    m->voltage_thd.value  = 3.5f;
    m->voltage_thd.limit  = 5.0f;
    m->current_thd.value = 12.0f;
    m->current_thd.limit = 8.0f;
}

/* ================================================================ */
/*                     USB ECM 传输层测试                            */
/* ================================================================ */

/**
 * @brief 测试 USB ECM 初始化与配置
 */
static int test_usb_ecm_init(void)
{
    usb_ecm_t ecm;

    /* 正常初始化（仿真地址） */
    TEST_ASSERT_EQ(usb_ecm_init(&ecm, "127.0.0.1", 9090), 0, "init normal");
    TEST_ASSERT(ecm.port == 9090, "port set");
    TEST_ASSERT(strcmp(ecm.host_ip, "127.0.0.1") == 0, "ip set");
    TEST_ASSERT(ecm.connected == 0, "not connected");
    TEST_ASSERT(ecm.sock == (intptr_t)0 || ecm.sock == (intptr_t)(-1) ||
                ecm.sock == (intptr_t)(~(uintptr_t)0),
                "sock invalid");

    /* 默认参数（NULL IP → 回环） */
    TEST_ASSERT_EQ(usb_ecm_init(&ecm, NULL, 0), 0, "init default");
    TEST_ASSERT(strcmp(ecm.host_ip, USB_ECM_SIM_IP) == 0, "default ip");
    TEST_ASSERT(ecm.port == USB_ECM_DEFAULT_PORT, "default port");

    /* 空指针校验 */
    TEST_ASSERT_EQ(usb_ecm_init(NULL, "127.0.0.1", 9090), -1, "null ecm");

    usb_ecm_disconnect(&ecm);
    return 0;
}

/**
 * @brief 测试 USB ECM 正常连接与通信（需算力模组仿真器运行）
 */
static int test_usb_ecm_normal_communication(void)
{
    usb_ecm_t ecm;
    const char *request = "{\"cmd\":\"ping\"}";
    char response[256];
    int ret;

    /* 启动算力模组仿真器 */
    TEST_ASSERT_EQ(compute_module_sim_start("127.0.0.1", TEST_PORT_NORMAL), 0,
                    "sim start");

    /* 初始化并连接 */
    TEST_ASSERT_EQ(usb_ecm_init(&ecm, "127.0.0.1", TEST_PORT_NORMAL), 0, "init");
    TEST_ASSERT_EQ(usb_ecm_connect(&ecm), 0, "connect");
    TEST_ASSERT(ecm.connected == 1, "connected flag");

    /* 发送数据 */
    ret = usb_ecm_send(&ecm, request, (int)strlen(request));
    TEST_ASSERT(ret > 0, "send ok");

    /* 接收应答（算力模组会返回推理结果或错误，这里只要收到数据即可） */
    ret = usb_ecm_recv(&ecm, response, sizeof(response), 2000);
    TEST_ASSERT(ret > 0, "recv ok");
    TEST_ASSERT(strlen(response) > 0, "response non-empty");

    /* 断开 */
    usb_ecm_disconnect(&ecm);
    TEST_ASSERT(ecm.connected == 0, "disconnected");

    compute_module_sim_stop();
    PQ_SLEEP_MS(100);
    return 0;
}

/**
 * @brief 测试 USB ECM 连接失败（目标不可达）
 */
static int test_usb_ecm_connect_refused(void)
{
    usb_ecm_t ecm;

    /* 连接一个未监听的端口（应失败） */
    TEST_ASSERT_EQ(usb_ecm_init(&ecm, "127.0.0.1", TEST_PORT_OFFLINE), 0, "init");
    TEST_ASSERT_EQ(usb_ecm_connect(&ecm), -1, "connect refused");
    TEST_ASSERT(ecm.connected == 0, "not connected after fail");

    /* request 应自动尝试连接，连接失败后返回错误 */
    {
        char resp[256];
        TEST_ASSERT_EQ(usb_ecm_request(&ecm, "test", resp, sizeof(resp), 1000),
                        -1, "request to dead port");
    }

    usb_ecm_disconnect(&ecm);
    return 0;
}

/**
 * @brief 测试 USB ECM request 请求-应答完整流程
 */
static int test_usb_ecm_request_response(void)
{
    usb_ecm_t ecm;
    char request[512];
    char response[512];
    feature_vector_t feat;
    pq_metrics_t metrics;
    int ret;

    /* 构造合法的 AI 推理请求 */
    build_test_features(&feat);
    build_test_metrics(&metrics);

    snprintf(request, sizeof(request),
             "{\"cmd\":\"infer\",\"features\":[");
    {
        int i, off = (int)strlen(request);
        for (i = 0; i < IF_N_FEATURES; i++) {
            off += snprintf(request + off, sizeof(request) - off,
                            "%.4f%s", feat.features[i],
                            (i < IF_N_FEATURES - 1) ? "," : "");
        }
        off += snprintf(request + off, sizeof(request) - off,
                        "],\"vthd\":%.3f,\"ithd\":%.3f}",
                        metrics.voltage_thd.value, metrics.current_thd.value);
    }

    /* 启动仿真器 */
    TEST_ASSERT_EQ(compute_module_sim_start("127.0.0.1", TEST_PORT_NORMAL), 0,
                    "sim start");

    /* 初始化并执行 request */
    TEST_ASSERT_EQ(usb_ecm_init(&ecm, "127.0.0.1", TEST_PORT_NORMAL), 0, "init");

    ret = usb_ecm_request(&ecm, request, response, sizeof(response), 2000);
    TEST_ASSERT_EQ(ret, 0, "request ok");

    /* 验证应答包含 JSON 字段 */
    TEST_ASSERT(strstr(response, "if") != NULL, "response has if");
    TEST_ASSERT(strstr(response, "ae") != NULL, "response has ae");
    TEST_ASSERT(strstr(response, "cls") != NULL, "response has cls");

    usb_ecm_disconnect(&ecm);
    compute_module_sim_stop();
    PQ_SLEEP_MS(100);
    return 0;
}

/**
 * @brief 测试 USB ECM 空参数校验
 */
static int test_usb_ecm_null_params(void)
{
    usb_ecm_t ecm;
    char buf[256];

    /* 初始化后再测试空参数 */
    TEST_ASSERT_EQ(usb_ecm_init(&ecm, "127.0.0.1", 9090), 0, "init");

    TEST_ASSERT_EQ(usb_ecm_connect(NULL), -1, "null ecm connect");
    TEST_ASSERT_EQ(usb_ecm_send(NULL, "data", 4), -1, "null ecm send");
    TEST_ASSERT_EQ(usb_ecm_send(&ecm, NULL, 4), -1, "null data send");
    TEST_ASSERT_EQ(usb_ecm_send(&ecm, "data", 0), -1, "zero len send");
    TEST_ASSERT_EQ(usb_ecm_recv(NULL, buf, sizeof(buf), 100), -1, "null ecm recv");
    TEST_ASSERT_EQ(usb_ecm_recv(&ecm, NULL, sizeof(buf), 100), -1, "null buf recv");
    TEST_ASSERT_EQ(usb_ecm_request(NULL, "req", buf, sizeof(buf), 100), -1,
                    "null ecm request");
    TEST_ASSERT_EQ(usb_ecm_request(&ecm, NULL, buf, sizeof(buf), 100), -1,
                    "null req request");
    TEST_ASSERT_EQ(usb_ecm_request(&ecm, "req", NULL, sizeof(buf), 100), -1,
                    "null resp request");

    usb_ecm_disconnect(&ecm);
    return 0;
}

/* ================================================================ */
/*                     AI RPC 客户端测试                             */
/* ================================================================ */

/**
 * @brief 测试 AI RPC 正常推理流程（算力模组在线）
 */
static int test_ai_rpc_normal_infer(void)
{
    feature_vector_t feat;
    pq_metrics_t metrics;
    ai_result_t result;

    build_test_features(&feat);
    build_test_metrics(&metrics);

    /* 启动算力模组仿真器 */
    TEST_ASSERT_EQ(compute_module_sim_start("127.0.0.1", TEST_PORT_NORMAL), 0,
                    "sim start");

    /* 初始化 AI RPC */
    TEST_ASSERT_EQ(ai_rpc_init("127.0.0.1", TEST_PORT_NORMAL), 0, "rpc init");

    /* 执行推理 */
    TEST_ASSERT_EQ(ai_rpc_infer(&feat, &metrics, &result), 0, "infer");

    /* 验证算力模组在线 */
    TEST_ASSERT_EQ(result.module_available, 1, "module online");
    TEST_ASSERT_EQ(ai_rpc_module_online(), 1, "online flag");

    /* 验证推理结果有效 */
    TEST_ASSERT(result.if_score >= 0.0f && result.if_score <= 1.0f,
                "if_score in [0,1]");
    TEST_ASSERT(result.cnn_class >= 0 && result.cnn_class < 7,
                "cnn_class valid");
    TEST_ASSERT(result.cnn_confidence >= 0.0f && result.cnn_confidence <= 1.0f,
                "confidence in [0,1]");

    ai_rpc_deinit();
    compute_module_sim_stop();
    PQ_SLEEP_MS(100);
    return 0;
}

/**
 * @brief 测试 AI RPC 降级场景 1：算力模组未启动
 */
static int test_ai_rpc_fallback_offline(void)
{
    feature_vector_t feat;
    pq_metrics_t metrics;
    ai_result_t result;

    build_test_features(&feat);
    build_test_metrics(&metrics);

    /* 不启动算力模组仿真器，直接初始化 RPC 指向未监听端口 */
    TEST_ASSERT_EQ(ai_rpc_init("127.0.0.1", TEST_PORT_OFFLINE), 0, "rpc init");

    /* 执行推理（应自动降级为本地推理） */
    TEST_ASSERT_EQ(ai_rpc_infer(&feat, &metrics, &result), 0, "infer");

    /* 验证降级标志 */
    TEST_ASSERT_EQ(result.module_available, 0, "module offline");
    TEST_ASSERT_EQ(ai_rpc_module_online(), 0, "offline flag");

    /* 验证本地推理结果仍然有效 */
    TEST_ASSERT(result.if_score >= 0.0f && result.if_score <= 1.0f,
                "local if_score in [0,1]");
    TEST_ASSERT(result.cnn_class >= 0 && result.cnn_class < 7,
                "local cnn_class valid");

    ai_rpc_deinit();
    return 0;
}

/**
 * @brief 测试 AI RPC 降级场景 2：算力模组运行中停止
 */
static int test_ai_rpc_fallback_mid_run(void)
{
    feature_vector_t feat;
    pq_metrics_t metrics;
    ai_result_t result;
    int i;

    build_test_features(&feat);
    build_test_metrics(&metrics);

    /* 启动算力模组仿真器 */
    TEST_ASSERT_EQ(compute_module_sim_start("127.0.0.1", TEST_PORT_NORMAL), 0,
                    "sim start");
    TEST_ASSERT_EQ(ai_rpc_init("127.0.0.1", TEST_PORT_NORMAL), 0, "rpc init");

    /* 前 3 次推理应 ONLINE */
    for (i = 0; i < 3; i++) {
        TEST_ASSERT_EQ(ai_rpc_infer(&feat, &metrics, &result), 0, "infer online");
        TEST_ASSERT_EQ(result.module_available, 1, "online");
    }
    TEST_ASSERT_EQ(ai_rpc_module_online(), 1, "was online");

    /* 停止算力模组仿真器 */
    compute_module_sim_stop();
    PQ_SLEEP_MS(200);

    /* 后续推理应降级为本地 */
    TEST_ASSERT_EQ(ai_rpc_infer(&feat, &metrics, &result), 0, "infer after stop");
    TEST_ASSERT_EQ(result.module_available, 0, "fallback to local");
    TEST_ASSERT_EQ(ai_rpc_module_online(), 0, "now offline");

    /* 验证本地推理结果仍然有效 */
    TEST_ASSERT(result.if_score >= 0.0f && result.if_score <= 1.0f,
                "local if_score valid");

    ai_rpc_deinit();
    return 0;
}

/**
 * @brief 测试 AI RPC 恢复场景：离线后算力模组恢复，自动重连
 */
static int test_ai_rpc_recovery(void)
{
    feature_vector_t feat;
    pq_metrics_t metrics;
    ai_result_t result;

    build_test_features(&feat);
    build_test_metrics(&metrics);

    /* 1. 初始化指向未监听端口，第一次推理降级 */
    TEST_ASSERT_EQ(ai_rpc_init("127.0.0.1", TEST_PORT_RECOVERY), 0, "rpc init");
    TEST_ASSERT_EQ(ai_rpc_infer(&feat, &metrics, &result), 0, "infer 1");
    TEST_ASSERT_EQ(result.module_available, 0, "first fallback");
    TEST_ASSERT_EQ(ai_rpc_module_online(), 0, "offline");

    /* 2. 启动算力模组仿真器 */
    TEST_ASSERT_EQ(compute_module_sim_start("127.0.0.1", TEST_PORT_RECOVERY), 0,
                    "sim start");
    PQ_SLEEP_MS(100);

    /* 3. 再次推理，应自动重连并恢复 ONLINE */
    TEST_ASSERT_EQ(ai_rpc_infer(&feat, &metrics, &result), 0, "infer 2");
    TEST_ASSERT_EQ(result.module_available, 1, "recovered online");
    TEST_ASSERT_EQ(ai_rpc_module_online(), 1, "online after recovery");

    /* 4. 验证推理结果来自算力模组 */
    TEST_ASSERT(result.if_score >= 0.0f && result.if_score <= 1.0f,
                "recovered if_score");

    ai_rpc_deinit();
    compute_module_sim_stop();
    PQ_SLEEP_MS(100);
    return 0;
}

/**
 * @brief 测试 AI RPC 多次推理稳定性
 */
static int test_ai_rpc_stability(void)
{
    feature_vector_t feat;
    pq_metrics_t metrics;
    ai_result_t result;
    int i;
    int online_count = 0;

    build_test_features(&feat);
    build_test_metrics(&metrics);

    TEST_ASSERT_EQ(compute_module_sim_start("127.0.0.1", TEST_PORT_NORMAL), 0,
                    "sim start");
    TEST_ASSERT_EQ(ai_rpc_init("127.0.0.1", TEST_PORT_NORMAL), 0, "rpc init");

    /* 执行 20 次推理 */
    for (i = 0; i < 20; i++) {
        if (ai_rpc_infer(&feat, &metrics, &result) != 0) {
            printf("  [FAIL] infer #%d failed\n", i + 1);
            return -1;
        }
        if (result.module_available) online_count++;
    }

    /* 20 次推理应全部 ONLINE */
    TEST_ASSERT_EQ(online_count, 20, "all 20 online");

    ai_rpc_deinit();
    compute_module_sim_stop();
    PQ_SLEEP_MS(100);
    return 0;
}

/**
 * @brief 测试 AI RPC 空参数校验
 */
static int test_ai_rpc_null_params(void)
{
    feature_vector_t feat;
    pq_metrics_t metrics;

    build_test_features(&feat);
    build_test_metrics(&metrics);

    TEST_ASSERT_EQ(ai_rpc_init("127.0.0.1", TEST_PORT_NORMAL), 0, "init");

    /* 空指针参数 */
    TEST_ASSERT_EQ(ai_rpc_infer(NULL, &metrics, NULL), -1, "null feat");
    TEST_ASSERT_EQ(ai_rpc_infer(&feat, NULL, NULL), -1, "null metrics");
    TEST_ASSERT_EQ(ai_rpc_infer(&feat, &metrics, NULL), -1, "null result");

    ai_rpc_deinit();
    return 0;
}

/* ================================================================ */
/*                 算力模组仿真器测试                                */
/* ================================================================ */

/**
 * @brief 测试算力模组仿真器启动与停止
 */
static int test_compute_module_sim_start_stop(void)
{
    TEST_ASSERT_EQ(compute_module_sim_start("127.0.0.1", TEST_PORT_NORMAL), 0,
                    "start");
    TEST_ASSERT_EQ(compute_module_sim_running(), 1, "running");

    /* 重复启动应返回 0（幂等） */
    TEST_ASSERT_EQ(compute_module_sim_start("127.0.0.1", TEST_PORT_NORMAL), 0,
                    "double start");

    compute_module_sim_stop();
    PQ_SLEEP_MS(100);
    TEST_ASSERT_EQ(compute_module_sim_running(), 0, "stopped");

    return 0;
}

/**
 * @brief 测试算力模组仿真器并发推理（多次请求）
 */
static int test_compute_module_sim_concurrent(void)
{
    usb_ecm_t ecm;
    feature_vector_t feat;
    pq_metrics_t metrics;
    char request[USB_ECM_MAX_PACKET];
    char response[512];
    int i, ret;

    build_test_features(&feat);
    build_test_metrics(&metrics);

    TEST_ASSERT_EQ(compute_module_sim_start("127.0.0.1", TEST_PORT_NORMAL), 0,
                    "sim start");
    TEST_ASSERT_EQ(usb_ecm_init(&ecm, "127.0.0.1", TEST_PORT_NORMAL), 0, "init");
    TEST_ASSERT_EQ(usb_ecm_connect(&ecm), 0, "connect");

    /* 在同一连接上发送 10 次推理请求 */
    for (i = 0; i < 10; i++) {
        int off;
        off = snprintf(request, sizeof(request), "{\"cmd\":\"infer\",\"features\":[");
        {
            int j;
            for (j = 0; j < IF_N_FEATURES; j++) {
                off += snprintf(request + off, sizeof(request) - off,
                                "%.4f%s", feat.features[j],
                                (j < IF_N_FEATURES - 1) ? "," : "");
            }
            off += snprintf(request + off, sizeof(request) - off,
                             "],\"vthd\":%.3f,\"ithd\":%.3f}",
                             metrics.voltage_thd.value, metrics.current_thd.value);
        }

        ret = usb_ecm_send(&ecm, request, (int)strlen(request));
        TEST_ASSERT(ret > 0, "send");

        ret = usb_ecm_recv(&ecm, response, sizeof(response), 2000);
        TEST_ASSERT(ret > 0, "recv");
        TEST_ASSERT(strstr(response, "if") != NULL, "has if field");
    }

    usb_ecm_disconnect(&ecm);
    compute_module_sim_stop();
    PQ_SLEEP_MS(100);
    return 0;
}

/* ================================================================ */
/*                          主函数                                   */
/* ================================================================ */

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("============================================================\n");
    printf("  USB ECM & AI RPC Unit Tests (v2.1.0)\n");
    printf("  T536 + HT7627S (host) <-> RK3576 (compute module)\n");
    printf("============================================================\n\n");

    /* ---- USB ECM 传输层测试 ---- */
    printf("--- USB ECM Transport Layer Tests ---\n");
    RUN_TEST(test_usb_ecm_init);
    RUN_TEST(test_usb_ecm_normal_communication);
    RUN_TEST(test_usb_ecm_connect_refused);
    RUN_TEST(test_usb_ecm_request_response);
    RUN_TEST(test_usb_ecm_null_params);

    /* ---- AI RPC 客户端测试 ---- */
    printf("--- AI RPC Client Tests ---\n");
    RUN_TEST(test_ai_rpc_normal_infer);
    RUN_TEST(test_ai_rpc_fallback_offline);
    RUN_TEST(test_ai_rpc_fallback_mid_run);
    RUN_TEST(test_ai_rpc_recovery);
    RUN_TEST(test_ai_rpc_stability);
    RUN_TEST(test_ai_rpc_null_params);

    /* ---- 算力模组仿真器测试 ---- */
    printf("--- Compute Module Simulator Tests ---\n");
    RUN_TEST(test_compute_module_sim_start_stop);
    RUN_TEST(test_compute_module_sim_concurrent);

    /* ---- 测试汇总 ---- */
    printf("============================================================\n");
    printf("  Test Summary\n");
    printf("============================================================\n");
    printf("  Total:  %d\n", g_tests_run);
    printf("  Passed: %d\n", g_tests_passed);
    printf("  Failed: %d\n", g_tests_failed);
    printf("  Result: %s\n", g_tests_failed == 0 ? "ALL PASSED" : "HAS FAILURES");
    printf("============================================================\n");

    return (g_tests_failed == 0) ? 0 : 1;
}
