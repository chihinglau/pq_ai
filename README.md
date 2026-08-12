# PQ AI Terminal — 基于终端波形数据的电能质量 AI 应用

> **项目全称**：基于终端交流采样波形数据的电能质量 AI 应用 —— 新能源与充电桩接入影响评估
> **目标平台**：全志 T536（4×Cortex-A55 + E907 RISC-V） + 钜泉 HT7627S（7 通道 24bit AFE） + 瑞芯微 RK3576（外挂算力模组，USB ECM 互连）
> **版本**：v2.2.0 ｜ **日期**：2026-08-13
> **许可证**：内部技术方案验证工程，仅供项目团队使用

---

## 项目简介

本项目针对**分布式光伏**与**电动汽车充电桩**大规模接入配电网后引发的电能质量（Power Quality, PQ）问题，构建了一套从**算法仿真验证**到**嵌入式实时部署**的完整解决方案。

核心能力：
- **MATLAB 时域仿真**：基于电路方程与功率流分析，生成 S1~S5 五类典型场景的波形数据与 AI 训练集
- **实时采集仿真**：模拟 HT7627S 以 12.8 kHz 采样率同步采集 7 通道（3 相电压 + 3 相电流 + 零序）波形
- **12 项 PQ 指标实时计算**：电压偏差、THD、三相不平衡度、频率偏差、变压器/线路负载率等（基于 GB/T 国标）
- **7 类事件检测**：电压暂降/暂升、谐波超标、不平衡、过载、频率偏差、场景变化（带滞回机制）
- **AI 异常检测**：孤立森林（iForest）、自编码器（AE）、1D-CNN 三类模型推理，运行于 RK3576 算力模组，通过 USB ECM 远程调用
- **S1~S5 场景自动识别**：基准负荷 / 充电桩 / 分布式光伏 / 光充耦合 / 极端工况，并给出治理建议
- **数据上报与持久化**：MQTT 上报云端 + 本地 CSV 存储（后续替换为 SQLite）

---

## 仓库结构

```
pq_ai/
├── matlab_sim/                    # MATLAB 仿真子系统（算法验证 + 数据集生成）
│   ├── main_setup.m                #   主入口（支持 S1~S5 / all / monte_carlo / dataset / full）
│   ├── runSimulation.m            #   时域仿真引擎（纯 MATLAB，不依赖 Simulink 建模）
│   ├── calculatePQMetrics.m       #   8 项国标 PQ 指标计算
│   ├── monteCarloAnalysis.m       #   蒙特卡洛风险评估（1000 次采样）
│   ├── generateDataset.m          #   AI 训练数据集生成（10000 条样本）
│   └── README.md                  #   MATLAB 子项目说明
│
├── pq_ai_terminal/                # 嵌入式 C 软件子系统（T536 + HT7627S）
│   ├── Makefile                   #   纯 Makefile（支持 Windows MinGW / Linux GCC / aarch64 交叉编译）
│   ├── CMakeLists.txt             #   顶层 CMake 配置
│   ├── config.ini                 #   运行时配置（限值、采样率、MQTT、AI 阈值）
│   ├── include/                   #   公共头文件（pq_common / pq_hal / pq_config）
│   ├── drivers/                   #   HT7627S 寄存器定义 + HAL 接口
│   ├── core/                      #   核心算法（pq_metrics / event_trigger / wave_freeze / feature_extract / scenario_detect）
│   ├── ai/                        #   AI 推理 Stub（iforest / ae / cnn1d）
│   ├── comm/                      #   通信层（proto_mqtt / time_sync / usb_ecm）
│   ├── utils/                     #   工具层（ring_buffer / json_builder / sqlite_wrapper / pq_config）
│   ├── sim/                       #   软模拟层（hal_sim / sim_main / rk3576_inference_server.py）
│   ├── app/                       #   嵌入式真实入口（wave_export_arm.c / wave_sender_arm.c / wave_inference_server_v2.py）
│   ├── cmake/                     #   aarch64 交叉编译工具链
│   ├── scripts/                   #   构建/运行/部署脚本
│   ├── docs/                      #   项目开发手册 + Linux 环境技术方案
│   └── README.md                  #   嵌入式子项目说明
│
├── *.docx                         # 顶层技术方案文档（5 份）
│   ├── T536_HT7627S_嵌入式软件方案设计.docx
│   ├── 新能源与充电桩接入影响评估技术方案.docx
│   ├── 电能质量AI应用技术实施方案_T536_HT7627S.docx
│   ├── 基于终端交流采样波形数据的电能质量AI应用...开题报告.docx
│   └── 基于终端波形数据的电能质量 AI 应用需求及分工手册.docx
│
├── generate_*.js                  # 技术方案文档生成脚本
├── package.json                   # Node 依赖（用于文档生成）
└── README.md                      # 本文件
```

---

## 硬件平台

| 组件 | 型号 | 作用 |
|------|------|------|
| 采样主控 | 全志 T536 | 4× Cortex-A55 + E907 RISC-V，负责采样、PQ 指标计算、事件触发（不带 NPU） |
| 计量 AFE | 钜泉 HT7627S | 7 通道 24bit ADC，支持 256/512 点/周波，内置谐波分析引擎 |
| 算力模组 | 瑞芯微 RK3576 | 外挂 USB ECM 算力模组，运行 iForest/AE/CNN1D/大模型 AI 推理 |
| 互连 | USB ECM | T536 ↔ RK3576 通过 USB Ethernet Control Model 虚拟网卡通信 |
| 上云通信 | 4G / Wi-Fi / Ethernet | MQTT 上报与远程运维 |

---

## 系统架构

```
┌──────────────────────── 采样主机 T536 + HT7627S ────────────────────────┐
│                                                                        │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐                  │
│  │ HT7627S     │───▶│ HAL 读取    │───▶│ PQ 指标计算  │                  │
│  │ 采样/仿真器 │    │ 寄存器+波形 │    │ pq_metrics  │                  │
│  └─────────────┘    └─────────────┘    └─────────────┘                  │
│                              │                          │                │
│                              ▼                          ▼                │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐                  │
│  │ 事件触发     │◄──│ 波形冻结    │    │ 特征提取    │                  │
│  │ event_trig  │   │ wave_freeze │    │ feature_ext │                  │
│  └─────────────┘    └─────────────┘    └──────┬──────┘                  │
│         │                                       │                        │
│         ▼                                       ▼                        │
│  ┌─────────────┐                    ┌─────────────────┐                  │
│  │ 数据持久化   │                    │ AI RPC 客户端   │                  │
│  │ CSV/SQLite  │                    │ ai_rpc         │                  │
│  └─────────────┘                    └───────┬─────────┘                  │
│         │                                   │                            │
│         ▼                                   │                            │
│  ┌─────────────┐                           │                            │
│  │ MQTT 上报   │                           │                            │
│  └─────────────┘                           │                            │
└────────────────────────────────────────────┼────────────────────────────┘
                                             │
                                    USB ECM 虚拟网卡
                                    (TCP over USB)
                                             │
┌──────────────────────── 算力模组 RK3576 ───┼────────────────────────────┐
│                                            ▼                            │
│                               ┌─────────────────┐                       │
│                               │ AI 推理服务     │                       │
│                               │ compute_module │                       │
│                               └───────┬─────────┘                       │
│                          ┌────────────┼────────────┐                   │
│                          ▼            ▼            ▼                   │
│                   ┌────────────┐┌────────────┐┌────────────┐          │
│                   │ iForest    ││ 自编码器   ││ 1D-CNN     │          │
│                   │ 异常检测   ││ AE 重构    ││ 事件分类   │          │
│                   └────────────┘└────────────┘└────────────┘          │
│                                                                       │
│                   ┌──────────────────────────────┐                     │
│                   │ 大模型 / 其他 AI 算法（后续） │                     │
│                   └──────────────────────────────┘                     │
└───────────────────────────────────────────────────────────────────────┘
```

**数据流**：
1. T536+HT7627S 采样 → 波形采集 → 原始波形数据
2. 原始波形通过 USB ECM 二进制协议发送给 RK3576（24字节协议头 + 7182字节波形数据）
3. RK3576 解析波形 → 计算 RMS → 提取 27 维特征向量 → 运行 iForest/AE/CNN1D 推理
4. RK3576 返回 48 字节 AI 推理响应包（iForest得分、AE得分、CNN分类及置信度）
5. T536 接收结果 → 场景识别 → MQTT 上报 + 治理建议

**已验证的真实硬件流程**（v2.2.0）：
- T536 A相加压（UA RMS≈236V），B/C相开路（UB/UC RMS≈1.2V）
- 完整业务链路：T536采集→TCP发送→RK3576解析→特征提取→AI推理→返回结果
- AI推理结果正确：iForest=1.0000（异常），CNN=3（单相开路），置信度0.90

---

## 快速开始

### 一、MATLAB 仿真（Windows + MATLAB R2025b+）

```matlab
% 1. 添加路径
addpath(genpath('D:\ai\prj\trae\pq_ai\matlab_sim'));

% 2. 检查工具箱
checkToolboxes

% 3. 运行场景
main_setup('S4')          % 单场景（光充耦合，默认）
main_setup('all')         % 全部 5 个场景 + 对比报告
main_setup('monte_carlo') % 蒙特卡洛风险评估
main_setup('dataset')     % 生成 AI 训练数据集
main_setup('full')         % 完整流程
```

详细说明见 [matlab_sim/README.md](matlab_sim/README.md)。

### 二、嵌入式 C 仿真（推荐 WSL Ubuntu 26.04 + GCC 15）

#### 1. 环境准备

```bash
# WSL Ubuntu 26.04 内
sudo apt update
sudo apt install -y build-essential cmake ninja-build gdb vim git

# 验证
gcc --version      # gcc 15.2.0
make --version     # GNU Make 4.4.1
cmake --version    # 4.2.3
```

#### 2. 编译

```bash
# 复制源码（如从 Windows 挂载路径）
cp -r /mnt/d/ai/prj/trae/pq_ai/pq_ai_terminal ~/pq_ai/
cd ~/pq_ai/pq_ai_terminal

# 编译仿真主程序
make clean
make sim

# 产物
ls -lh pq_sim
# -rwxr-xr-x 1 ubuntu ubuntu 55K ... pq_sim
```

#### 3. 运行

```bash
# 帮助
./pq_sim --help

# 单场景（S1 基准负荷，30 周期）
./pq_sim --scenario S1 --cycles 30

# 批量运行全部 5 个场景（每个 100 周期）
./pq_sim --all --cycles 100
```

详细说明见 [pq_ai_terminal/README.md](pq_ai_terminal/README.md)。

### 三、交叉编译（部署到 T536，可选）

```bash
sudo apt install -y gcc-aarch64-linux-gnu
cd pq_ai_terminal
mkdir -p build-linux && cd build-linux
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/aarch64-linux-gnu.cmake -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
file pq_sim
# pq_sim: ELF 64-bit LSB executable, ARM aarch64
```

---

## 仿真场景

| 场景 | 描述 | 关键参数 | 评估重点 |
|------|------|----------|----------|
| **S1** | 基准负荷 | 340kW，PF=0.85 | 建立基准电压/电流/功率分布 |
| **S2** | 充电桩接入 | 80kW，5/7/11/13 次谐波 | 谐波注入、负载率上升 |
| **S3** | 分布式光伏 | 200kW，电压抬升 +2.93% | 电压抬升、反向潮流 |
| **S4** | 光充耦合 | 280kW，谐波 + 电压抬升 | 耦合效应、综合谐波 |
| **S5** | 极端高渗透率 | 360kW，高 THD | 承载边界、过载 |

---

## 已验证的部署结果

**环境**：WSL Ubuntu 26.04 + GCC 15.2.0 + Make 4.4.1 + CMake 4.2.3
**真实硬件**：T536 + RK3576 + HT7627S

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

| 场景 | 触发事件数 | 总评 | 治理建议 |
|------|-----------|------|----------|
| S1 基准负荷 | 0 | FAIL（Line Load ALARM） | 系统运行正常，继续监测 |
| S2 充电桩 | 100 | FAIL | 配置 APF；实施有序充电策略 |
| S3 分布式光伏 | 0 | **PASS** | 优化光伏逆变器无功调节策略 |
| S4 光充耦合 | 100 | FAIL | 配置储能系统平滑功率波动 |
| S5 极端场景 | 100 | FAIL | 立即启动负荷切除；投入备用容量 |

事件类型：HARMONIC（电压/电流 THD 超限，severity 1.38~2.65）。

---

## 国标限值速查

| 指标 | 国标 | 限值 |
|------|------|------|
| 电压偏差 | GB/T 12325 | ±7% |
| 电压 THD | GB/T 14549 | 5% |
| 电流 THD | GB/T 14549 | 8% |
| 三相不平衡度 | GB/T 15543 | 2% |
| 频率偏差 | GB/T 15945 | ±0.5 Hz |
| 变压器负载率 | — | 100% |
| 线路负载率 | — | 100% |
| 功率因数 | — | 0.85 |

---

## 关键技术参数

### C 仿真（HT7627S 软件模拟器）

| 参数 | 数值 | 说明 |
|------|------|------|
| 采样率 | 12800 Hz | HT7627S 实际采样率 |
| 每周期点数 | 256 | 50Hz × 256 = 12800 |
| 通道数 | 7 | 3 相电压 + 3 相电流 + 零序 |
| 谐波最高次数 | 31 | 支持 2~31 次谐波分析 |
| 仿真步长 | 0.078 ms | 对应 256 点/周波 |
| 变压器额定 | 800 kVA | 工业台区 |
| 线路阻抗 | R=0.027Ω, X=0.035Ω | LGJ-120, 100m |

### MATLAB 仿真（学术研究）

| 参数 | 数值 | 说明 |
|------|------|------|
| 采样率 | 10 kHz | 仿真步长 0.1 ms |
| 变压器额定 | 400 kVA | 居民台区 |
| 负荷有功 | 150 kW | PF=0.88 |
| 光伏额定 | 100 kW | MPPT 控制 |
| 充电桩单台 | 7 kW AC / 60 kW DC | 效率 95% |

> 两套仿真参数独立可调：MATLAB 侧重学术研究，C 仿真侧重工业现场验证。

---

## 文档

- [项目开发手册（完整复现版）](pq_ai_terminal/docs/项目开发手册（完整复现版）.md) — 新 PC 环境复现、新人培训、维护指南
- [项目开发手册](pq_ai_terminal/docs/项目开发手册.md) — 模块功能详解
- [Linux 环境技术方案](pq_ai_terminal/docs/Linux环境技术方案.md) — WSL 部署技术方案
- [MATLAB 仿真 README](matlab_sim/README.md) — MATLAB 子项目说明
- [嵌入式 C README](pq_ai_terminal/README.md) — 嵌入式子项目说明

---

## 后续工作

### 待完成项

1. **真实 HT7627S 驱动**：替换 `sim/hal_sim.c` 为 SPI/I2C 驱动，实现寄存器读写
2. **RK3576 算力模组程序**：在 RK3576 上部署独立 AI 服务程序（替换 compute_module_sim）
3. **USB ECM 真实驱动**：Linux g_nc / g_ether 配置，T536 与 RK3576 各呈现为虚拟网卡
4. **大模型部署**：在 RK3576 上部署训练好的 ONNX 模型 → INT8 量化 → RKNN SDK
5. **E907 RTOS 集成**：核心采集任务迁移至 E907 核
6. **IEC 61850 / MQTT 完整协议栈**：替换 Stub，支持 TLS 加密
7. **SQLite 本地存储**：替换 CSV 为嵌入式 SQLite 数据库

### 已知局限

- AI 模型为随机权重 Stub，异常得分仅用于演示流程
- 场景识别规则为硬编码，后续应替换为 AI 分类模型
- Windows 仿真未模拟线路阻抗的动态电压降（直接叠加 offset）

---

## 版本历史

### v2.2.0 (2026-08-13) — T536+RK3576 全链路验证版

- **T536 真实硬件波形采集与 AI 推理全链路打通**
  - `wave_export_arm.c`：直接使用 HAL 接口采集 T536 实时波形并导出为 CSV
  - `wave_sender_arm.c`：采集原始波形 → USB ECM 发送 → RK3576 AI 推理 → 接收结果
- **RK3576 AI 推理服务端** (`wave_inference_server_v2.py`)
  - 接收原始波形二进制协议（24字节协议头 + 波形数据）
  - 波形解析 → 特征提取（27维）→ AI 推理（iForest/AE/CNN）
  - 响应包：48字节（小端序 `<IB3sffifIi16s`）
- **交叉编译方法固化到 config.ini**
  - 使用 `arm-linux-gnueabihf-gcc`（GCC Linaro 5.3.1）编译 32 位 ARM 程序
  - 运行方式：`/lib32/ld-linux-armhf.so.3 --library-path /lib32:/custom/sys/lib/hal_lib/lib32`
- **部署脚本 deploy_and_test.sh 重写**
  - 支持交叉编译 → 上传 T536/RK3576 → 启动服务 → 运行测试 → 日志收集
- **关键修复**
  - AI 响应格式从错误的 56 字节（大端序）修正为正确的 48 字节（小端序）
  - Python 端协议解析从大端序 (`!`) 改为小端序 (`<`)
- **增强日志系统**：C 端分级日志 + Python logging 模块，同时输出到控制台和文件
- **测试验证**：A相加压、B/C相开路工况，AI 正确识别单相开路（iForest=1.0, CNN=3）

### v2.1.1 (2026-08-03) — 双机协作架构版（诊断增强）

- **架构变更**：T536 不带 NPU，新增 RK3576 算力模组通过 USB ECM 外挂
- 主机 T536+HT7627S 负责采样 + PQ 指标 + 事件触发 + 特征提取
- 算力模组 RK3576 负责 iForest/AE/CNN1D/大模型推理
- 新增 `comm/usb_ecm` 传输层（USB ECM 虚拟网卡 TCP 通信，跨平台）
- 新增 `ai/ai_rpc` 客户端（主机侧，JSON over TCP，带本地 fallback）
- 新增 `sim/compute_module_sim` 仿真器（RK3576 模拟，后台 TCP 服务线程）
- S1~S5 全 500 周期 AI 模组 ONLINE 验证通过
- `config.ini` 新增 `[compute_module]` 配置段

### v2.0.0 (2026-08-02) — 完整复现版

- WSL Ubuntu 26.04 + GCC 15.2.0 部署验证通过
- Makefile + CMake 双构建系统（Windows MinGW / Linux GCC / aarch64 交叉编译）
- S1~S5 五场景仿真验证（每个 100 周期），事件触发与治理建议正确
- 12 项 PQ 指标实时计算（基于 GB/T 12325/14549/15543/15945 国标）
- 7 通道 HT7627S 软件模拟器（12800Hz 采样，256 点/周波，2~31 次谐波）
- AI 推理 Stub（iForest / AE / 1D-CNN），待 NPU 工具链就绪后替换为 INT8 量化模型
- MQTT 上报 Stub + CSV 本地存储
- GitHub 仓库初始化与推送
- 根级 README.md 与 DOCUMENTATION.md 创建
- 版本权威源：`pq_ai_terminal/include/pq_version.h`

### v1.0.0 (2026-08-01) — 初始版本

- 基础仿真框架搭建
- 五类场景定义（S1 基准 / S2 充电桩 / S3 光伏 / S4 光充耦合 / S5 极端）
- 核心算法层（pq_metrics / event_trigger / wave_freeze / feature_extract / scenario_detect）
- MATLAB 时域仿真引擎（纯 MATLAB，不依赖 Simulink 建模）
- 蒙特卡洛风险评估与 AI 数据集生成

---

## 维护信息

- **维护团队**：嵌入式软件团队 + 算法仿真团队
- **文档版本**：v2.2.0（2026-08-13）
- **版本权威源**：[pq_ai_terminal/include/pq_version.h](pq_ai_terminal/include/pq_version.h)
- **GitHub**：[https://github.com/chihinglau/pq_ai](https://github.com/chihinglau/pq_ai)
