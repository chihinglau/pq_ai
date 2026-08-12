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
#define PQ_VERSION_MINOR    2
#define PQ_VERSION_PATCH    1

#define PQ_VERSION_STRING   "2.2.1"
#define PQ_VERSION_DATE     "2026-08-12"
#define PQ_VERSION_TITLE    "T536+RK3576 全链路验证版（密码修正）"

/* ==================== 版本宏便捷接口 ==================== */
#define PQ_VERSION_NUM      ((PQ_VERSION_MAJOR << 16) | \
                             (PQ_VERSION_MINOR << 8)  | \
                             (PQ_VERSION_PATCH))

/* ==================== 变更日志（CHANGELOG） ==================== */
/**
 * v2.2.1 (2026-08-12) —— 密码修正与运维文档增强
 *   - 更正 T536 SSH 密码: 从旧密码修正为 Iot@csg123
 *     涉及文件: config.ini, 项目开发手册, deploy_and_test.sh, generate_tech_report.py
 *   - 新增 Q16 FAQ: T536 SSH 账户被锁定（pam_tally2 解锁方法）
 *   - 发现并排查 T536 csg 账户 21 次失败登录导致的 PAM 锁定问题
 *   - 生成对外技术方案报告 Word 版 (PQ_AI_Terminal_技术方案报告_v2.2.0.docx)
 *
 * v2.2.0 (2026-08-13) —— T536+RK3576 全链路验证版
 *   - T536 真实硬件波形采集与 AI 推理全链路打通:
 *     wave_export_arm.c: 直接使用 HAL 接口采集 T536 实时波形并导出为 CSV
 *     wave_sender_arm.c: 采集原始波形 → USB ECM 发送 → RK3576 AI 推理 → 接收结果
 *   - RK3576 AI 推理服务端 (wave_inference_server_v2.py):
 *     接收原始波形二进制协议 (24字节协议头 + 波形数据)
 *     波形解析 → 特征提取 (27维) → AI 推理 (iForest/AE/CNN)
 *     响应包: 48字节 (小端序, <IB3sffifIi16s)
 *   - 跨编译方法固化到 config.ini:
 *     使用 arm-linux-gnueabihf-gcc (GCC Linaro 5.3.1) 编译 32位 ARM 程序
 *     运行方式: /lib32/ld-linux-armhf.so.3 --library-path /lib32:/custom/sys/lib/hal_lib/lib32
 *   - 部署脚本 deploy_and_test.sh 重写:
 *     支持交叉编译 → 上传 T536/RK3576 → 启动服务 → 运行测试 → 日志收集
 *     完整彩色分级日志输出
 *   - 关键修复:
 *     AI 响应格式从错误的 56字节 (大端序) 修正为正确的 48字节 (小端序)
 *     Python 端协议解析从大端序 (!) 改为小端序 (<)
 *     T536 32位动态链接器配置修复
 *   - 增强日志系统:
 *     wave_export_arm.c / wave_sender_arm.c 实现分级日志 (ERROR/WARN/INFO/DEBUG)
 *     wave_inference_server_v2.py 使用 Python logging 模块实现分级日志
 *     日志同时输出到控制台和文件
 *   - 测试验证结果:
 *     T536 A相加压 (UA RMS≈236V), B/C相开路 (UB/UC RMS≈1.2V)
 *     AI 推理正确识别单相开路工况: iForest=1.0000, CNN=3 (single phase open)
 *     业务流程: T536采集→TCP→RK3576解析→AI推理→返回结果 全链路验证通过
 *
 * v2.1.1 (2026-08-03) —— 双机协作架构版（诊断增强）
 *   - USB ECM 传输层（comm/usb_ecm.c）添加详细诊断日志：
 *     connect/send/recv/request 关键节点记录 errno、字节数、往返耗时
 *   - AI RPC 客户端（ai/ai_rpc.c）添加推理全链路日志：
 *     请求构建、推理结果、降级触发、恢复重连、累计统计（success_rate）
 *   - 算力模组仿真器（sim/compute_module_sim.c）添加推理各阶段耗时分解：
 *     parse/sleep/infer/send 各阶段微秒级计时
 *   - 新增 tests/test_usb_ecm_ai_rpc.c 单元测试（13 项测试，覆盖正常+降级+恢复场景）
 *   - Makefile 新增 test 目标，支持 mingw32-make test 一键编译运行
 *   - 重构 Linux环境技术方案.md（14 章完整技术方案，含 USB ECM 驱动配置与双机部署）
 *   - 重构 pq_ai_terminal/README.md（双机架构图、模块说明、验证项表）
 *
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
