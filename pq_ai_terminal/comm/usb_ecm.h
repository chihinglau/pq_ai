/**
 * @file usb_ecm.h
 * @brief USB ECM (Ethernet Control Model) 传输层接口
 *
 * 通过 USB ECM 虚拟网卡在 T536 主机与 RK3576 算力模组之间建立
 * TCP/IP 通信通道。真实部署时两端各呈现为一张以太网网卡
 * （如 169.254.1.1 / 169.254.1.2），仿真模式下退化为本地回环。
 *
 * @author PQ AI Terminal Team
 * @date 2026-08-02
 */

#ifndef USB_ECM_H
#define USB_ECM_H

#include "pq_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* USB ECM 虚拟网卡默认地址 —— 真实部署 */
#define USB_ECM_HOST_IP        "169.254.1.1"   /* T536 主机侧 */
#define USB_ECM_MODULE_IP      "169.254.1.2"   /* RK3576 算力模组侧 */
#define USB_ECM_DEFAULT_PORT   9090             /* AI 推理服务端口 */

/* 仿真模式回环地址 */
#define USB_ECM_SIM_IP         "127.0.0.1"

/* 单次收发最大长度 */
#define USB_ECM_MAX_PACKET     8192

/**
 * @brief USB ECM 传输句柄
 */
typedef struct {
    char host_ip[32];       /**< 对端 IP（算力模组） */
    int  port;              /**< 端口 */
    int  connected;         /**< 连接状态 0/1 */
    intptr_t sock;          /**< 平台 socket 句柄（Linux:int, Windows:SOCKET） */
} usb_ecm_t;

/**
 * @brief 初始化 USB ECM 传输（平台 socket 子系统初始化）
 * @param ecm  传输句柄
 * @param module_ip 算力模组 IP（仿真用 127.0.0.1）
 * @param port 端口
 * @return 0 成功
 */
int usb_ecm_init(usb_ecm_t *ecm, const char *module_ip, int port);

/**
 * @brief 连接算力模组
 * @param ecm 传输句柄
 * @return 0 成功，-1 失败
 */
int usb_ecm_connect(usb_ecm_t *ecm);

/**
 * @brief 发送数据
 * @param ecm 传输句柄
 * @param data 发送缓冲
 * @param len  长度
 * @return >0 已发送字节数，<=0 失败
 */
int usb_ecm_send(usb_ecm_t *ecm, const char *data, int len);

/**
 * @brief 接收数据
 * @param ecm 传输句柄
 * @param buf 接收缓冲
 * @param buf_size 缓冲大小
 * @param timeout_ms 超时（毫秒），0 表示阻塞
 * @return >0 已接收字节数，0 对端关闭，<0 失败/超时
 */
int usb_ecm_recv(usb_ecm_t *ecm, char *buf, int buf_size, int timeout_ms);

/**
 * @brief 断开连接
 */
void usb_ecm_disconnect(usb_ecm_t *ecm);

/**
 * @brief 请求-应答式通信（发送后等待接收）
 * @param ecm 传输句柄
 * @param req 请求 JSON
 * @param resp 应答缓冲
 * @param resp_size 应答缓冲大小
 * @param timeout_ms 超时
 * @return 0 成功
 */
int usb_ecm_request(usb_ecm_t *ecm, const char *req,
                    char *resp, int resp_size, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* USB_ECM_H */
