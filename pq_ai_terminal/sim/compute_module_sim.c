/**
 * @file compute_module_sim.c
 * @brief RK3576 算力模组仿真器实现
 *
 * 以本机 TCP 服务线程模拟 RK3576 算力模组：
 *   - 监听端口，接收主机发来的特征向量
 *   - 调用本地 iForest / AE / CNN1D Stub 推理
 *   - 返回 JSON 应答
 *
 * @author PQ AI Terminal Team
 * @date 2026-08-02
 */

/* 启用 POSIX 扩展（nanosleep 等），需在所有 include 之前定义 */
#ifndef PLATFORM_WINDOWS
#define _POSIX_C_SOURCE 200112L
#endif

#include "compute_module_sim.h"
#include "usb_ecm.h"
#include "iforest_infer.h"
#include "ae_infer.h"
#include "cnn1d_infer.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if defined(PLATFORM_WINDOWS) || defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef SOCKET socket_t;
    typedef HANDLE thread_t;
    #define SOCK_INVALID  INVALID_SOCKET
    #define SOCK_CLOSE     closesocket
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <pthread.h>
    typedef int socket_t;
    typedef pthread_t thread_t;
    #define SOCK_INVALID  (-1)
    #define SOCK_CLOSE     close
#endif

/* ==================== 模块状态 ==================== */
static volatile int g_running = 0;
static socket_t     g_listen_fd = SOCK_INVALID;
static thread_t     g_thread;

/* 本地 AI 模型（算力模组上运行） */
static iforest_model_t g_if_model;
static int             g_ai_loaded = 0;

/* ==================== 跨平台线程 ==================== */
#if defined(PLATFORM_WINDOWS) || defined(_WIN32)
static DWORD WINAPI server_thread_main(LPVOID arg);
#else
static void *server_thread_main(void *arg);
#endif

/* ==================== 解析请求中的特征向量 ==================== */
static int parse_infer_request(const char *req, float *features, int n_feat,
                                float *vthd, float *ithd)
{
    const char *p;
    int i;

    /* 找 "features":[ 开始 */
    p = strstr(req, "\"features\"");
    if (!p) p = strstr(req, "features");
    if (!p) return -1;

    p = strchr(p, '[');
    if (!p) return -1;
    p++;

    for (i = 0; i < n_feat; i++) {
        features[i] = strtof(p, (char **)&p);
        if (*p == ',') p++;
        else if (*p != ']') { /* 可能解析结束 */ }
    }

    /* 可选：解析 vthd / ithd */
    *vthd = 0.0f;
    *ithd = 0.0f;
    p = strstr(req, "\"vthd\"");
    if (p) {
        p = strchr(p, ':');
        if (p) { p++; *vthd = strtof(p, NULL); }
    }
    p = strstr(req, "\"ithd\"");
    if (p) {
        p = strchr(p, ':');
        if (p) { p++; *ithd = strtof(p, NULL); }
    }

    return 0;
}

/* ==================== 构建应答 JSON ==================== */
static int build_infer_response(char *buf, int buf_size,
                                 float if_score, float ae_score,
                                 int cnn_class, float cnn_conf,
                                 int latency_ms)
{
    /* 紧凑 JSON，避免浮点精度问题用 %.4f */
    int n = snprintf(buf, (size_t)buf_size,
        "{\"if\":%.4f,\"ae\":%.4f,\"cls\":%d,\"conf\":%.4f,\"lat\":%d}",
        if_score, ae_score, cnn_class, cnn_conf, latency_ms);
    return (n > 0 && n < buf_size) ? 0 : -1;
}

/* ==================== 处理单个推理请求 ==================== */
static void handle_infer(socket_t client_fd, const char *request)
{
    float features[IF_N_FEATURES];
    float vthd, ithd;
    float if_score, ae_score;
    float probs[CNN_MAX_CLASSES];
    float cnn_conf = 0.0f;
    int   cnn_class = 0;
    char  response[512];

    /* 解析请求 */
    if (parse_infer_request(request, features, IF_N_FEATURES, &vthd, &ithd) != 0) {
        const char *err = "{\"error\":\"bad request\"}";
        send(client_fd, err, (int)strlen(err), 0);
        return;
    }

    /* 模拟推理耗时 */
#if defined(PLATFORM_WINDOWS) || defined(_WIN32)
    Sleep(1);  /* 1ms 模拟 NPU 推理 */
#else
    struct timespec ts = {0, 1000000}; /* 1ms = 1000000 ns */
    nanosleep(&ts, NULL);
#endif

    /* iForest 推理 */
    if_score = iforest_score(&g_if_model, features);

    /* AE 推理 */
    ae_score = ae_anomaly_score(features);

    /* CNN 分类（使用特征向量模拟波形输入） */
    cnn1d_classify(features, 1, IF_N_FEATURES, 1, probs);
    cnn_class = cnn1d_get_class(probs, 7, &cnn_conf);

    /* 构建应答 */
    build_infer_response(response, sizeof(response),
                         if_score, ae_score, cnn_class, cnn_conf, 1);
    send(client_fd, response, (int)strlen(response), 0);
}

/* ==================== TCP 服务主循环 ==================== */
#if defined(PLATFORM_WINDOWS) || defined(_WIN32)
static DWORD WINAPI server_thread_main(LPVOID arg)
#else
static void *server_thread_main(void *arg)
#endif
{
    socket_t listen_fd = (socket_t)(intptr_t)arg;
    socket_t client_fd;
    char buf[USB_ECM_MAX_PACKET];
    int n;

    PQ_LOGI("compute_module_sim: server thread started");

    while (g_running) {
        struct sockaddr_in caddr;
#if defined(PLATFORM_WINDOWS) || defined(_WIN32)
        int alen = sizeof(caddr);
#else
        socklen_t alen = sizeof(caddr);
#endif

        client_fd = accept(listen_fd, (struct sockaddr *)&caddr, &alen);
        if (client_fd == SOCK_INVALID) {
            if (g_running) PQ_LOGW("compute_module_sim: accept failed");
            continue;
        }

        PQ_LOGI("compute_module_sim: host connected");

        /* 处理该连接的所有请求 */
        while (g_running) {
            n = (int)recv(client_fd, buf, sizeof(buf) - 1, 0);
            if (n <= 0) break;
            buf[n] = '\0';
            handle_infer(client_fd, buf);
        }

        SOCK_CLOSE(client_fd);
        PQ_LOGI("compute_module_sim: host disconnected");
    }

    PQ_LOGI("compute_module_sim: server thread exiting");
    return 0;
}

/* ==================== 公开接口 ==================== */
int compute_module_sim_start(const char *listen_ip, int port)
{
    socket_t listen_fd;
    struct sockaddr_in addr;

    if (g_running) {
        PQ_LOGW("compute_module_sim: already running");
        return 0;
    }

    /* 初始化 AI 模型（算力模组上的模型） */
    if (!g_ai_loaded) {
        iforest_load_model(&g_if_model, NULL);
        ae_init(IF_N_FEATURES, 8);
        cnn1d_init(7, 256);
        g_ai_loaded = 1;
        PQ_LOGI("compute_module_sim: AI models loaded (RK3576 emulation)");
    }

#if defined(PLATFORM_WINDOWS) || defined(_WIN32)
    /* Windows 需要 WSAStartup */
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        PQ_LOGE("compute_module_sim: WSAStartup failed");
        return -1;
    }
#endif

    /* 创建监听 socket */
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == SOCK_INVALID) {
        PQ_LOGE("compute_module_sim: socket() failed");
        return -1;
    }

    /* 允许地址复用 */
    {
        int opt = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
                   (const char *)&opt, sizeof(opt));
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
#ifdef PLATFORM_WINDOWS
    addr.sin_addr.s_addr = inet_addr(listen_ip ? listen_ip : USB_ECM_SIM_IP);
#else
    inet_pton(AF_INET, listen_ip ? listen_ip : USB_ECM_SIM_IP, &addr.sin_addr);
#endif

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        PQ_LOGE("compute_module_sim: bind() failed on %s:%d",
                listen_ip ? listen_ip : USB_ECM_SIM_IP, port);
        SOCK_CLOSE(listen_fd);
        return -1;
    }

    if (listen(listen_fd, 1) == -1) {
        PQ_LOGE("compute_module_sim: listen() failed");
        SOCK_CLOSE(listen_fd);
        return -1;
    }

    g_listen_fd = listen_fd;
    g_running = 1;

    /* 启动服务线程 */
#if defined(PLATFORM_WINDOWS) || defined(_WIN32)
    g_thread = CreateThread(NULL, 0, server_thread_main,
                            (LPVOID)(intptr_t)listen_fd, 0, NULL);
    if (g_thread == NULL) {
        PQ_LOGE("compute_module_sim: CreateThread failed");
        g_running = 0;
        SOCK_CLOSE(listen_fd);
        return -1;
    }
#else
    if (pthread_create(&g_thread, NULL, server_thread_main,
                       (void *)(intptr_t)listen_fd) != 0) {
        PQ_LOGE("compute_module_sim: pthread_create failed");
        g_running = 0;
        SOCK_CLOSE(listen_fd);
        return -1;
    }
#endif

    PQ_LOGI("compute_module_sim: RK3576 emulation listening on %s:%d",
            listen_ip ? listen_ip : USB_ECM_SIM_IP, port);
    return 0;
}

void compute_module_sim_stop(void)
{
    if (!g_running) return;

    g_running = 0;

    if (g_listen_fd != SOCK_INVALID) {
        SOCK_CLOSE(g_listen_fd);
        g_listen_fd = SOCK_INVALID;
    }

    /* 等待线程退出 */
#if defined(PLATFORM_WINDOWS) || defined(_WIN32)
    WaitForSingleObject(g_thread, 2000);
    CloseHandle(g_thread);
#else
    pthread_join(g_thread, NULL);
#endif

#if defined(PLATFORM_WINDOWS) || defined(_WIN32)
    WSACleanup();
#endif

    PQ_LOGI("compute_module_sim: stopped");
}

int compute_module_sim_running(void)
{
    return g_running;
}
