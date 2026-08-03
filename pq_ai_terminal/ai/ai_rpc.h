/**
 * @file ai_rpc.h
 * @brief AI 推理 RPC 客户端（主机侧）
 *
 * 主机 T536+HT7627S 通过 USB ECM 将特征向量发送给 RK3576 算力模组，
 * 算力模组运行 iForest / AE / CNN1D / 大模型后返回推理结果。
 *
 * 当算力模组不可用时，自动降级为本地 Stub 推理（保证仿真不中断）。
 *
 * @author PQ AI Terminal Team
 * @date 2026-08-02
 */

#ifndef AI_RPC_H
#define AI_RPC_H

#include "pq_common.h"
#include "pq_metrics.h"
#include "feature_extract.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief AI 推理结果（算力模组返回）
 */
typedef struct {
    float if_score;         /**< iForest 异常得分 [0,1] */
    float ae_score;         /**< AE 重构误差（MSE） */
    int   cnn_class;        /**< CNN 事件类别（0=正常, 1=暂降, ...） */
    float cnn_confidence;   /**< CNN 置信度 [0,1] */
    int   latency_ms;       /**< 算力模组处理耗时（ms） */
    int   module_available; /**< 1=算力模组响应, 0=降级为本地 */
} ai_result_t;

/**
 * @brief 初始化 AI RPC 客户端
 * @param module_ip 算力模组 IP（仿真用 127.0.0.1）
 * @param port 端口
 * @return 0 成功
 */
int ai_rpc_init(const char *module_ip, int port);

/**
 * @brief 执行一次 AI 推理（同步）
 *
 * 流程：组装特征 → USB ECM 发送 → 等待应答 → 解析结果。
 * 若算力模组超时或不可达，自动调用本地 Stub 推理。
 *
 * @param feat 特征向量
 * @param metrics 当前 PQ 指标（作为辅助输入）
 * @param result 推理结果输出
 * @return 0 成功
 */
int ai_rpc_infer(const feature_vector_t *feat,
                 const pq_metrics_t *metrics,
                 ai_result_t *result);

/**
 * @brief 查询算力模组是否在线
 * @return 1 在线，0 离线
 */
int ai_rpc_module_online(void);

/**
 * @brief 反初始化，释放资源
 */
void ai_rpc_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* AI_RPC_H */
