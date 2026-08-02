# PQ AI Terminal — 开发部署文档

> **版本**：v2.0.0 ｜ **日期**：2026-08-02 ｜ **状态**：已验证
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

**目标平台**：全志 T536（4×Cortex-A55 + 2T NPU + E907 RISC-V） + 钜泉 HT7627S（7 通道 24bit 高精度计量 AFE）

**核心能力**：
- MATLAB 时域仿真（S1~S5 五场景 + 蒙特卡洛 + AI 数据集）
- C 实时采集仿真（HT7627S 软件模拟器，12800Hz 采样）
- 12 项 PQ 指标实时计算（GB/T 国标）
- 7 类事件检测（带滞回机制）
- AI 异常检测（iForest / AE / 1D-CNN，当前 Stub）
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

**编译**：16 个源文件全部干净编译（`-std=c99 -Wall -Wextra -O2 -DPLATFORM_LINUX`，无警告）。

`./pq_sim --all --cycles 100` 运行结果：

| 场景 | 触发事件数 | 总评 | 事件类型 | 治理建议 |
|------|-----------|------|----------|----------|
| S1 基准负荷 | 0 | FAIL | — | 系统运行正常，继续监测 |
| S2 充电桩 | 100 | FAIL | HARMONIC（电压 THD） | 配置 APF；实施有序充电策略 |
| S3 分布式光伏 | 0 | **PASS** | — | 优化光伏逆变器无功调节策略 |
| S4 光充耦合 | 100 | FAIL | HARMONIC（电流 THD） | 配置储能系统平滑功率波动 |
| S5 极端场景 | 100 | FAIL | HARMONIC（电流 THD） | 立即启动负荷切除；投入备用容量 |

---

## 6. 关键功能实现记录

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
2. **NPU 模型部署**：ONNX → INT8 量化 → T536 NPU SDK
3. **E907 RTOS 集成**：核心采集任务迁移至 E907 核
4. **rpmsg 核间通信**：E907 与 A55 之间的消息队列
5. **IEC 61850 / MQTT 完整协议栈**：替换 Stub，支持 TLS
6. **SQLite 本地存储**：替换 CSV 为嵌入式 SQLite

### 7.2 已知局限

- AI 模型为随机权重 Stub，异常得分仅用于演示流程
- 场景识别规则为硬编码，后续应替换为 AI 分类模型
- Windows 仿真未模拟线路阻抗的动态电压降

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
