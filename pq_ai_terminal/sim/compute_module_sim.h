/**
 * @file compute_module_sim.h
 * @brief RK3576 算力模组仿真器（仿真模式专用）
 *
 * 在无真实 RK3576 硬件时，以本机 TCP 服务线程模拟算力模组行为：
 *   1. 监听 USB ECM 端口（默认 127.0.0.1:9090）
 *   2. 接收主机发来的特征向量 JSON
 *   3. 运行 iForest / AE / CNN1D 本地 Stub 推理
 *   4. 返回 JSON 应答
 *
 * 真实部署时此模块不编译，由 RK3576 侧独立程序替代。
 *
 * @author PQ AI Terminal Team
 * @date 2026-08-02
 */

#ifndef COMPUTE_MODULE_SIM_H
#define COMPUTE_MODULE_SIM_H

#include "pq_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动算力模组仿真器（后台线程）
 * @param listen_ip 监听 IP（仿真用 127.0.0.1）
 * @param port 端口
 * @return 0 成功，-1 失败
 */
int compute_module_sim_start(const char *listen_ip, int port);

/**
 * @brief 停止算力模组仿真器
 */
void compute_module_sim_stop(void);

/**
 * @brief 查询仿真器是否运行中
 * @return 1 运行中，0 已停止
 */
int compute_module_sim_running(void);

#ifdef __cplusplus
}
#endif

#endif /* COMPUTE_MODULE_SIM_H */
