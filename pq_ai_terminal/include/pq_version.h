/**
 * @file pq_version.h
 * @brief 项目版本信息（版本权威来源）
 * @author PQ AI Terminal Team
 * @date 2026-08-02
 *
 * 推送 GitHub 前必须同步刷新以下三处版本信息：
 *   1. 本文件（pq_version.h）            —— C 源码中的版本权威来源
 *   2. README.md                          —— GitHub 仓库首页展示
 *   3. DOCUMENTATION.md                   —— 开发部署文档
 *
 * 版本号规则：主版本号.次版本号.修订号
 *   - Major：架构级变更或不兼容更新
 *   - Minor：新增功能模块
 *   - Patch：Bug 修复、小优化
 */

#ifndef PQ_VERSION_H
#define PQ_VERSION_H

/* ==================== 当前版本（权威来源） ==================== */
#define PQ_VERSION_MAJOR    2
#define PQ_VERSION_MINOR    1
#define PQ_VERSION_PATCH    0

#define PQ_VERSION_STRING   "2.1.0"
#define PQ_VERSION_DATE     "2026-08-02"
#define PQ_VERSION_TITLE    "双机协作架构版"

/* ==================== 版本宏便捷接口 ==================== */
#define PQ_VERSION_NUM      ((PQ_VERSION_MAJOR << 16) | \
                             (PQ_VERSION_MINOR << 8)  | \
                             (PQ_VERSION_PATCH))

/* ==================== 变更日志（CHANGELOG） ==================== */
/**
 * v2.1.0 (2026-08-02) —— 双机协作架构版
 *   - 架构变更：T536 不带 NPU，新增 RK3576 算力模组通过 USB ECM 外挂
 *   - 主机 T536+HT7627S 负责采样 + PQ 指标 + 事件触发 + 特征提取
 *   - 算力模组 RK3576 负责 iForest/AE/CNN1D/大模型推理
 *   - 新增 comm/usb_ecm 传输层（USB ECM 虚拟网卡 TCP 通信，跨平台）
 *   - 新增 ai/ai_rpc 客户端（主机侧，JSON over TCP，带本地 fallback）
 *   - 新增 sim/compute_module_sim 仿真器（RK3576 模拟，后台 TCP 服务线程）
 *   - sim_main.c 改为通过 USB ECM RPC 调用 AI，500 周期全部 ONLINE
 *   - config.ini 新增 [compute_module] 配置段
 *
 * v2.0.0 (2026-08-02) —— 完整复现版
 *   - WSL Ubuntu 26.04 + GCC 15.2.0 部署验证通过
 *   - Makefile + CMake 双构建系统（支持 Windows MinGW / Linux GCC / aarch64 交叉编译）
 *   - S1~S5 五场景仿真验证（每个 100 周期），事件触发与治理建议正确
 *   - 12 项 PQ 指标实时计算（基于 GB/T 12325/14549/15543/15945 国标）
 *   - 7 通道 HT7627S 软件模拟器（12800Hz 采样，256 点/周波，2~31 次谐波）
 *   - AI 推理 Stub（iForest / AE / 1D-CNN），待 NPU 工具链就绪后替换为 INT8 量化模型
 *   - MQTT 上报 Stub + CSV 本地存储
 *   - GitHub 仓库初始化与推送（https://github.com/chihinglau/pq_ai）
 *   - 根级 README.md 与 DOCUMENTATION.md 创建
 *
 * v1.0.0 (2026-08-01) —— 初始版本
 *   - 基础仿真框架搭建
 *   - 五类场景定义（S1 基准 / S2 充电桩 / S3 光伏 / S4 光充耦合 / S5 极端）
 *   - 核心算法层（pq_metrics / event_trigger / wave_freeze / feature_extract / scenario_detect）
 *   - MATLAB 时域仿真引擎（纯 MATLAB，不依赖 Simulink 建模）
 *   - 蒙特卡洛风险评估与 AI 数据集生成
 */

#endif /* PQ_VERSION_H */
