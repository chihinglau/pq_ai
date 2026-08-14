# PQ AI Terminal — 开发部署文档

> **版本**：v2.4.0 ｜ **日期**：2026-08-14 ｜ **状态**：已验证
> **版本权威源**：`pq_ai_terminal/include/pq_version.h`
> **GitHub**：[https://github.com/chihinglau/pq_ai](https://github.com/chihinglau/pq_ai)

---

## 1. 文档说明

本文档为 PQ AI Terminal 项目的开发部署总览，与下列文档互补：

| 文档 | 路径 | 用途 |
|------|------|------|
| 项目开发手册（完整复现版） | `pq_ai_terminal/docs/项目开发手册（完整复现版）.md` | 新 PC 环境复现、新人培训 |
| 项目开发手册 | `pq_ai_terminal/docs/项目开发手册.md` | 模块功能详解 |
| Linux 环境技术方案 | `pq_ai_terminal/docs/Linux环境技术方案.md` | WSL 部署技术方案 |
| MATLAB 子项目 README | `matlab_sim/README.md` | MATLAB 仿真说明 |
| 嵌入式子项目 README | `pq_ai_terminal/README.md` | 嵌入式 C 软件说明 |
| 根级 README | `README.md` | GitHub 仓库首页 |

---

## 2. 项目概述

**项目全称**：基于终端交流采样波形数据的电能质量 AI 应用 —— 新能源与充电桩接入影响评估

**目标平台**：全志 T536（4×Cortex-A55 + E907 RISC-V） + 钜泉 HT7627S（7 通道 24bit AFE） + 瑞芯微 RK3576（外挂算力模组，USB ECM 互连）

**核心能力**：
- MATLAB 时域仿真（S1~S5 五场景 + 蒙特卡洛 + AI 数据集）
- C 实时采集仿真（HT7627S 软件模拟器，12800Hz 采样）
- 12 项 PQ 指标实时计算（GB/T 国标）
- 7 类事件检测（带滞回机制）
- **T536 真实硬件波形采集 + USB ECM 传输 + RK3576 AI 推理**
- AI 异常检测（iForest / AE / 1D-CNN）
- 场景识别与治理建议

---

## 3. 环境要求

### 3.1 C 仿真（推荐 WSL Ubuntu 26.04）

| 工具 | 版本要求 | 验证版本 |
|------|----------|----------|
| Ubuntu | 24.04+ | 26.04 |
| GCC | 13+（手册 15.0.1） | 15.2.0 |
| Make | 4.0+ | 4.4.1 |
| CMake | 3.28+ | 4.2.3 |
| Ninja | 1.10+ | 1.13.2 |
| Git | 2.30+ | 2.53.0 |

可选（交叉编译）：`gcc-aarch64-linux-gnu`

### 3.2 MATLAB 仿真

| 工具 | 版本要求 |
|------|----------|
| MATLAB | R2025b 或更高 |
| 操作系统 | Windows / Linux / macOS |

> 仿真本身为纯 MATLAB 实现，不依赖 Simulink / Simscape Electrical（仅工具箱检查需要）。

### 3.3 Windows 原生（备选）

| 工具 | 版本 |
|------|------|
| MSYS2 MinGW-w64 | GCC 13+ |
| CMake + Ninja | 随 MSYS2 安装 |

---

## 4. 快速部署（WSL Ubuntu 26.04）

### 4.1 环境准备

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build gdb vim git
```

### 4.2 获取源码

```bash
# 方式一：从 Windows 挂载路径复制
cp -r /mnt/d/ai/prj/trae/pq_ai/pq_ai_terminal ~/pq_ai/
cd ~/pq_ai/pq_ai_terminal

# 方式二：从 GitHub 克隆
git clone https://github.com/chihinglau/pq_ai.git
cd pq_ai/pq_ai_terminal
```

### 4.3 编译

```bash
make clean
make sim
```

预期产物：
```
-rwxr-xr-x 1 ubuntu ubuntu 55K ... pq_sim
```

### 4.4 运行验证

```bash
# 帮助
./pq_sim --help

# 单场景
./pq_sim --scenario S1 --cycles 30

# 全部 5 个场景
./pq_sim --all --cycles 100
```

---

## 5. 已验证的部署结果

**环境**：WSL Ubuntu 26.04 + GCC 15.2.0 + Make 4.4.1 + CMake 4.2.3
**真实硬件**：T536 + RK3576 + HT7627S

### v2.4.0 RK3576 NPU 推理全链路验证

| 环节 | 状态 | 说明 |
|------|------|------|
| RKNN 模型部署 | ✅ | cnn1d_8class.rknn (1.4MB) |
| NPU 推理服务 | ✅ | wave_inference_server_v5_npu.py |
| V2 协议通信 | ✅ | 7194字节波形帧，CRC32校验 |
| AI 响应解析 | ✅ | 63字节扩展格式，7通道有效值 |
| RKLLM 共存 | ✅ | Core 0 (RKLLM) + Core 1 (RKNN NPU) |
| 推理性能 | ✅ | 2-3ms/周期，3/3成功 |

### v2.2.0 真实硬件全链路验证

| 环节 | 状态 | 说明 |
|------|------|------|
| T536 HAL 初始化 | ✅ | HAL init → device get 成功 |
| 波形采集 | ✅ | 7194字节/周期, 5周期成功 |
| USB ECM 通信 | ✅ | T536(192.168.100.2) ↔ RK3576(192.168.100.1) |
| 原始波形传输 | ✅ | 24字节协议头 + 7182字节波形, 小端序 |
| RK3576 波形解析 | ✅ | 7通道 × 256点 × 4字节float 正确解析 |
| 特征提取 | ✅ | UA=236.705V, UB=1.224V, UC=1.214V |
| AI 推理 | ✅ | iForest=1.0000, CNN=3(单相开路), 置信度0.90 |
| 响应接收 | ✅ | 48字节小端序响应包, 魔数验证通过 |
| 日志系统 | ✅ | 分级日志(ERROR/WARN/INFO/DEBUG) 控制台+文件 |

### v2.1.0 仿真验证

| 场景 | 触发事件数 | 总评 | AI 模组 | 事件类型 | 治理建议 |
|------|-----------|------|---------|----------|----------|
| S1 基准负荷 | 0 | FAIL | **ONLINE** | — | 系统运行正常，继续监测 |
| S2 充电桩 | 100 | FAIL | **ONLINE** | HARMONIC（电压 THD） | 配置 APF；实施有序充电策略 |
| S3 分布式光伏 | 0 | **PASS** | **ONLINE** | — | 优化光伏逆变器无功调节策略 |
| S4 光充耦合 | 100 | FAIL | **ONLINE** | HARMONIC（电流 THD） | 配置储能系统平滑功率波动 |
| S5 极端场景 | 100 | FAIL | **ONLINE** | HARMONIC（电流 THD） | 立即启动负荷切除；投入备用容量 |

> 全 500 周期 AI 算力模组（RK3576 via USB ECM）均保持 ONLINE。

---

## 6. 关键功能实现记录

### v2.4.0 (2026-08-14) — RK3576 NPU 推理部署与 AI 响应扩展版

| 模块 | 状态 | 说明 |
|------|------|------|
| RKNN 模型部署 | ✅ 完成 | cnn1d_8class.rknn 模型部署到 RK3576 |
| NPU 推理服务 | ✅ 完成 | wave_inference_server_v5_npu.py 基于 RKNN Toolkit Lite2 |
| NPU 核心隔离 | ✅ 完成 | Core 1 (RKNN NPU) + Core 0 (RKLLM) 共存 |
| AI 响应扩展 | ✅ 完成 | 35→63 字节，添加 7 通道有效值 |
| V2 协议更新 | ✅ 完成 | 波形数据格式与通信协议详解 v1.2 |
| 交叉编译固化 | ✅ 完成 | GCC Linaro 5.3.1 编译 ARM 程序 |
| 真实硬件验证 | ✅ 验证 | 3/3 周期 100% 成功 |

**v2.4.0 验证结果**：

| 环节 | 状态 | 说明 |
|------|------|------|
| T536 波形采集 | ✅ | A相加压(UA RMS≈235V), B/C相开路(UB/UC≈1.2V) |
| USB ECM 传输 | ✅ | 7194字节波形帧，CRC32校验通过 |
| RKNN NPU 推理 | ✅ | CNN=7(three_loss), 置信度 0.92, 耗时 2-3ms |
| AI 响应解析 | ✅ | 63字节扩展格式，7通道有效值正确 |
| RKLLM 共存 | ✅ | RKLLM + RKNN 双模型共存验证通过 |

**新增文档**：
- `pq_ai_terminal/docs/RK3576_NPU模型训练与部署技术文档.md`
- `pq_ai_terminal/docs/波形数据格式与通信协议详解.md` (更新至 v1.2)

### v2.3.1 (2026-08-13) — RKLLM 集成与异常检测优化版

| 模块 | 状态 | 说明 |
|------|------|------|
| RKLLM 大模型部署 | ✅ 完成 | qwen3-1.7b-rk3576 模型部署，端口 8080 |
| RKLLM 服务管理 | ✅ 完成 | rk3576_ai_service.sh 集成 RKLLM 启停、状态、日志管理 |
| LLM 异常分析集成 | ✅ 完成 | AI 推理异常时自动调用 RKLLM 进行根因分析 |
| 异常检测逻辑优化 | ✅ 完成 | Heuristic 模式多指标综合检测（CV+电压偏差+不平衡度） |
| 异常判定阈值调整 | ✅ 完成 | IF > 0.4 或 CNN 异常 或 AE > 100 触发异常 |
| RKLLM 连通性测试 | ✅ 完成 | test_rkllm_connectivity.py 验证脚本 |
| 真实硬件 LLM 验证 | ✅ 验证 | T536→RK3576→LLM 全链路验证通过 |
| 文档更新 | ✅ 完成 | README.md/DOCUMENTATION.md/项目开发手册同步更新 |

**v2.3.1 验证结果**：

| 环节 | 状态 | 说明 |
|------|------|------|
| T536 波形采集 | ✅ | A相加压(UA RMS≈236V), B/C相开路(UB/UC≈1.2V) |
| USB ECM 传输 | ✅ | 24字节协议头 + 7182字节波形, CRC32校验通过 |
| Heuristic AI 推理 | ✅ | IF=0.8000(异常), CNN=3(严重异常), 置信度0.90 |
| LLM 异常触发 | ✅ | 检测到 IF>0.4, 自动调用 RKLLM |
| RKLLM 根因分析 | ✅ | 耗时95.88s, 评估"high", 返回异常解释+治理建议 |
| RKLLM 服务管理 | ✅ | systemctl 管理, 支持手动启停和状态查看 |

**RKLLM 部署架构**：

```
┌─────────────────────────────────────────────┐
│              RK3576 算力模组                │
│                                             │
│  ┌─────────────┐   ┌───────────────────┐   │
│  │ AI 推理服务 │──▶│ RKLLM 大模型服务   │   │
│  │ (9090端口)   │   │ (8080端口)        │   │
│  └──────┬──────┘   └───────────────────┘   │
│         │                          ▲        │
│         │                          │        │
│  ┌──────┴──────┐           ┌──────┴──────┐ │
│  │ Heuristic/  │           │ qwen3-1.7b │ │
│  │ NPU 推理    │           │ .rkllm      │ │
│  └─────────────┘           └──────────────┘ │
└─────────────────────────────────────────────┘
```

**RKLLM 服务管理命令**：

```bash
# 查看 RKLLM 服务状态
systemctl status rkllm-server

# 启动/停止 RKLLM 服务
systemctl start rkllm-server
systemctl stop rkllm-server

# 查看 RKLLM 日志
journalctl -u rkllm-server -f

# 或使用项目管理脚本
./scripts/rk3576_ai_service.sh rkllm-status
./scripts/rk3576_ai_service.sh rkllm-restart
```

**RKLLM API 接口**：

| 接口 | URL | 说明 |
|------|-----|------|
| 健康检查 | GET http://127.0.0.1:8080/health | 返回服务状态 |
| 模型列表 | GET http://127.0.0.1:8080/v1/models | 返回可用模型 |
| 聊天补全 | POST http://127.0.0.1:8080/v1/chat/completions | 发送对话请求 |
| 流式输出 | POST http://127.0.0.1:8080/v1/chat/completions | stream=true 参数 |

### v2.3.0 (2026-08-13) — 字节序、重连机制与推理模式修复版

| 模块 | 状态 | 说明 |
|------|------|------|
| AI 推理模式重构 | ✅ 完成 | 新增 Heuristic/NPU 双模式，支持运行时切换 |
| Heuristic 模式 | ✅ 完成 | 基于物理规则的确定性计算，稳定可靠 |
| NPU 模式自动回退 | ✅ 完成 | 检测无效结果（NaN/Inf/全零）时自动回退 |
| 字节序处理 | ✅ 完成 | T536(ARM32)↔RK3576(ARM64) 小端序统一，read_le64() 实现 |
| 服务端重连机制 | ✅ 完成 | 保存监听 socket，reaccept() 支持客户端断开后重连 |
| CRC32 修复 | ✅ 完成 | 多项式 0xEDB88320，与 Python zlib.crc32 兼容 |
| 手动测试指南 | ✅ 完成 | T536/RK3576 完整手动测试步骤 |
| 文档更新 | ✅ 完成 | README.md/DOCUMENTATION.md/项目开发手册同步更新 |

### v2.2.1 (2026-08-12) — 密码修正与运维文档增强

| 模块 | 状态 | 说明 |
|------|------|------|
| T536 SSH 密码更正 | ✅ 完成 | 从旧密码修正为 Iot@csg123（config.ini/文档/脚本） |
| Q16 FAQ 新增 | ✅ 完成 | T536 SSH 账户锁定排查与解锁方法（pam_tally2） |
| 账户锁定排查 | ✅ 验证 | 21 次失败登录导致 PAM 锁定，已解锁 |
| 技术方案报告 | ✅ 完成 | Word 版对外技术方案报告生成 |

### v2.2.0 (2026-08-13) — T536+RK3576 全链路验证版

| 模块 | 状态 | 说明 |
|------|------|------|
| wave_export_arm.c | ✅ 完成 | T536 端波形采集导出工具，直接 HAL 接口，分级日志 |
| wave_sender_arm.c | ✅ 完成 | T536 端波形采集发送程序，原始波形→USB ECM→AI推理→接收结果 |
| wave_inference_server_v2.py | ✅ 完成 | RK3576 端 AI 推理服务，分级日志，48字节响应 |
| config.ini 固化 | ✅ 完成 | 交叉编译方法、运行方法、协议格式、IP配置 |
| deploy_and_test.sh | ✅ 完成 | 一键部署脚本，彩色分级日志，自动收集测试结果 |
| 协议格式修复 | ✅ 完成 | AI响应从56字节(大端)→48字节(小端<IB3sffifIi16s) |
| 真实硬件验证 | ✅ 验证 | A相加压B/C相开路，AI正确识别单相开路 |

### v2.1.1 (2026-08-03) — 双机协作架构版（诊断增强）

| 模块 | 状态 | 说明 |
|------|------|------|
| USB ECM 诊断日志（comm/usb_ecm.c） | ✅ 完成 | connect/send/recv/request 关键节点记录 errno、字节数、往返耗时 |
| AI RPC 全链路日志（ai/ai_rpc.c） | ✅ 完成 | 请求构建、推理结果、降级触发、恢复重连、累计统计（success_rate） |
| 仿真器耗时分解（sim/compute_module_sim.c） | ✅ 完成 | parse/sleep/infer/send 各阶段微秒级计时 |
| USB ECM + AI RPC 单元测试 | ✅ 完成 | 13 项测试，覆盖正常流程+异常降级+恢复场景，全部通过 |
| Makefile test 目标 | ✅ 完成 | `mingw32-make test` / `make test` 一键编译运行 |
| Linux 环境技术方案重构 | ✅ 完成 | 14 章完整方案，含 USB ECM 驱动配置与双机部署路径 |
| pq_ai_terminal/README.md 重构 | ✅ 完成 | 双机架构图、模块说明、验证项表、版本历史更新 |

### v2.1.0 (2026-08-02) — 双机协作架构版

| 模块 | 状态 | 说明 |
|------|------|------|
| USB ECM 传输层（comm/usb_ecm.c） | ✅ 完成 | 跨平台 TCP socket（Winsock2/BSD），USB ECM 虚拟网卡通信 |
| AI RPC 客户端（ai/ai_rpc.c） | ✅ 完成 | JSON over TCP，带本地 fallback 降级 |
| 算力模组仿真器（sim/compute_module_sim.c） | ✅ 完成 | RK3576 模拟，后台 TCP 服务线程，运行 iForest/AE/CNN1D |
| sim_main.c 双机架构 | ✅ 完成 | 启动算力模组 → ai_rpc 推理 → 优雅关闭 |
| config.ini 算力模组配置 | ✅ 完成 | [compute_module] 段（enabled/ip/port） |
| S1~S5 全场景 USB ECM 验证 | ✅ 验证 | 500 周期 AI 模组全部 ONLINE |
| T536 NPU 依赖移除 | ✅ 完成 | T536 仅负责采样+指标，AI 推理迁移至 RK3576 |

### v2.0.0 (2026-08-02) — 完整复现版

| 模块 | 状态 | 说明 |
|------|------|------|
| HAL 仿真层（hal_sim.c） | ✅ 完成 | HT7627S 软件模拟器，7 通道 12800Hz 采样 |
| PQ 指标计算（pq_metrics.c） | ✅ 完成 | 12 项指标，基于 GB/T 国标 |
| 事件触发引擎（event_trigger.c） | ✅ 完成 | 6 类事件检查，滞回机制（90%/92%） |
| 波形冻结（wave_freeze.c） | ✅ 完成 | 环形缓冲 + 事件触发冻结 |
| 特征提取（feature_extract.c） | ✅ 完成 | 27 维特征向量 |
| 场景识别（scenario_detect.c） | ✅ 完成 | S1~S5 规则判定 |
| AI 推理 Stub（iforest/ae/cnn1d） | ✅ Stub | 随机权重，待 NPU 部署 |
| MQTT 通信（proto_mqtt.c） | ✅ Stub | JSON 上报，待 TLS 加密 |
| 时间同步（time_sync.c） | ✅ Stub | NTP/PTP 待实现 |
| 数据持久化（sqlite_wrapper.c） | ✅ CSV | 待替换为 SQLite |
| Makefile 构建 | ✅ 完成 | Windows MinGW / Linux GCC / aarch64 交叉编译 |
| CMake 构建 | ✅ 完成 | 支持 Ninja 生成器 |
| WSL Ubuntu 26.04 部署 | ✅ 验证 | GCC 15.2.0，S1~S5 全场景通过 |
| GitHub 仓库 | ✅ 创建 | https://github.com/chihinglau/pq_ai |
| 版本管理三件套 | ✅ 创建 | pq_version.h + README + DOCUMENTATION.md |

### v1.0.0 (2026-08-01) — 初始版本

| 模块 | 状态 | 说明 |
|------|------|------|
| MATLAB 时域仿真引擎 | ✅ 完成 | 纯 MATLAB，不依赖 Simulink |
| 五类场景定义 | ✅ 完成 | S1~S5 |
| 蒙特卡洛风险评估 | ✅ 完成 | 1000 次采样 |
| AI 数据集生成 | ✅ 完成 | 10000 条样本，20 维特征 |

---

## 7. 后续工作

### 7.1 待完成项

1. **真实 HT7627S 驱动**：替换 `sim/hal_sim.c` 为 SPI/I2C 驱动
2. **E907 RTOS 集成**：核心采集任务迁移至 E907 核
3. **IEC 61850 / MQTT 完整协议栈**：替换 Stub，支持 TLS 加密
4. **SQLite 本地存储**：替换 CSV 为嵌入式 SQLite 数据库
5. **更多 AI 模型部署**：部署 iForest 和 AE 模型到 RK3576 NPU

### 7.2 已完成项

1. ~~RK3576 算力模组程序~~：✅ 已部署 `wave_inference_server_v5_npu.py`
2. ~~大模型部署~~：✅ RKLLM qwen3-1.7b 已部署
3. ~~USB ECM 真实驱动~~：✅ T536 ↔ RK3576 USB ECM 通信已验证
4. ~~RKNN 模型部署~~：✅ cnn1d_8class.rknn 已部署并验证

### 7.3 已知局限

- AI 模型仅包含 CNN1D，iForest 和 AE 为启发式实现
- 场景识别规则为硬编码，后续应替换为 AI 分类模型

---

## 8. 版本管理规范

### 8.1 推送前必做

每次向 GitHub 推送代码前，必须同步刷新以下三处版本信息：

| 文件 | 更新内容 |
|------|----------|
| `pq_ai_terminal/include/pq_version.h` | `PQ_VERSION_*` 宏 + CHANGELOG 注释 |
| `README.md` | 顶部版本号 + 版本历史章节 |
| `DOCUMENTATION.md` | 顶部版本号 + 关键功能实现记录章节 |

### 8.2 版本号规则

- **主版本号（Major）**：架构级变更或不兼容更新
- **次版本号（Minor）**：新增功能模块
- **修订号（Patch）**：Bug 修复、小优化

### 8.3 检查清单

推送前确认：
- [ ] `pq_version.h` 中 `PQ_VERSION_STRING` / `PQ_VERSION_DATE` 已更新
- [ ] `pq_version.h` 中 CHANGELOG 注释已插入新版本条目
- [ ] `README.md` 顶部版本号已更新
- [ ] `README.md` 版本历史章节已插入新版本
- [ ] `DOCUMENTATION.md` 顶部版本号已更新
- [ ] `DOCUMENTATION.md` 关键功能实现记录已添加新版本
- [ ] commit message 包含版本号（如 `v2.0.0: ...`）

---

## 9. 联系

- **维护团队**：嵌入式软件团队 + 算法仿真团队
- **GitHub**：[https://github.com/chihinglau/pq_ai](https://github.com/chihinglau/pq_ai)
