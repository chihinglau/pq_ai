/**
 * @file usb_ecm.c
 * @brief USB ECM 传输层实现（跨平台 TCP socket）
 *
 * Windows: Winsock2 (ws2_32)
 * Linux:   BSD sockets
 *
 * @author PQ AI Terminal Team
 * @date 2026-08-02
 */

#include "usb_ecm.h"
#include <string.h>

#if defined(PLATFORM_WINDOWS) || defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef SOCKET socket_t;
    #define SOCK_INVALID  INVALID_SOCKET
    #define SOCK_CLOSE     closesocket
    #define SOCK_ERR       SOCKET_ERROR
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <sys/time.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <errno.h>
    typedef int socket_t;
    #define SOCK_INVALID  (-1)
    #define SOCK_CLOSE     close
    #define SOCK_ERR       (-1)
#endif

/* 平台 socket 子系统初始化计数 */
static int g_sock_init_count = 0;

static int platform_sock_init(void)
{
#if defined(PLATFORM_WINDOWS) || defined(_WIN32)
    if (g_sock_init_count == 0) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            PQ_LOGE("WSAStartup failed");
            return -1;
        }
    }
#endif
    g_sock_init_count++;
    return 0;
}

static void platform_sock_deinit(void)
{
    g_sock_init_count--;
#if defined(PLATFORM_WINDOWS) || defined(_WIN32)
    if (g_sock_init_count == 0) {
        WSACleanup();
    }
#endif
}

int usb_ecm_init(usb_ecm_t *ecm, const char *module_ip, int port)
{
    if (ecm == NULL) return -1;
    memset(ecm, 0, sizeof(*ecm));

    if (platform_sock_init() != 0) return -1;

    strncpy(ecm->host_ip,
            module_ip ? module_ip : USB_ECM_SIM_IP,
            sizeof(ecm->host_ip) - 1);
    ecm->port = (port > 0) ? port : USB_ECM_DEFAULT_PORT;
    ecm->connected = 0;
    ecm->sock = (intptr_t)SOCK_INVALID;
    return 0;
}

int usb_ecm_connect(usb_ecm_t *ecm)
{
    socket_t fd;
    struct sockaddr_in addr;

    if (ecm == NULL || ecm->connected) return -1;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == SOCK_INVALID) {
        PQ_LOGE("usb_ecm: socket() failed");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)ecm->port);
#ifdef PLATFORM_WINDOWS
    addr.sin_addr.s_addr = inet_addr(ecm->host_ip);
#else
    inet_pton(AF_INET, ecm->host_ip, &addr.sin_addr);
#endif

    PQ_LOGI("usb_ecm: connecting to %s:%d ...", ecm->host_ip, ecm->port);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == SOCK_ERR) {
        PQ_LOGW("usb_ecm: connect failed (%s:%d)", ecm->host_ip, ecm->port);
        SOCK_CLOSE(fd);
        return -1;
    }

    ecm->sock = (intptr_t)fd;
    ecm->connected = 1;
    PQ_LOGI("usb_ecm: connected to compute module %s:%d", ecm->host_ip, ecm->port);
    return 0;
}

int usb_ecm_send(usb_ecm_t *ecm, const char *data, int len)
{
    socket_t fd;
    int sent, total = 0;

    if (ecm == NULL || !ecm->connected || data == NULL || len <= 0) return -1;
    fd = (socket_t)ecm->sock;

    while (total < len) {
        sent = (int)send(fd, data + total, (size_t)(len - total), 0);
        if (sent == SOCK_ERR || sent == 0) {
            PQ_LOGE("usb_ecm: send failed");
            return -1;
        }
        total += sent;
    }
    return total;
}

int usb_ecm_recv(usb_ecm_t *ecm, char *buf, int buf_size, int timeout_ms)
{
    socket_t fd;
    int n;

    if (ecm == NULL || !ecm->connected || buf == NULL || buf_size <= 0) return -1;
    fd = (socket_t)ecm->sock;

    /* 超时设置 */
    if (timeout_ms > 0) {
#if defined(PLATFORM_WINDOWS) || defined(_WIN32)
        DWORD tv = (DWORD)timeout_ms;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
#else
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
#endif
    }

    n = (int)recv(fd, buf, (size_t)buf_size - 1, 0);
    if (n <= 0) {
        if (n == 0) {
            PQ_LOGW("usb_ecm: peer closed");
        }
        return n;
    }
    buf[n] = '\0';
    return n;
}

void usb_ecm_disconnect(usb_ecm_t *ecm)
{
    if (ecm == NULL) return;
    if (ecm->connected) {
        socket_t fd = (socket_t)ecm->sock;
        if (fd != SOCK_INVALID) {
            SOCK_CLOSE(fd);
        }
        ecm->connected = 0;
        ecm->sock = (intptr_t)SOCK_INVALID;
        PQ_LOGI("usb_ecm: disconnected");
    }
    platform_sock_deinit();
}

int usb_ecm_request(usb_ecm_t *ecm, const char *req,
                    char *resp, int resp_size, int timeout_ms)
{
    int ret;

    if (ecm == NULL || req == NULL || resp == NULL || resp_size <= 0) return -1;

    /* 确保连接 */
    if (!ecm->connected) {
        if (usb_ecm_connect(ecm) != 0) return -1;
    }

    /* 发送请求 */
    ret = usb_ecm_send(ecm, req, (int)strlen(req));
    if (ret <= 0) return -1;

    /* 接收应答 */
    ret = usb_ecm_recv(ecm, resp, resp_size, timeout_ms);
    if (ret <= 0) return -1;

    return 0;
}
