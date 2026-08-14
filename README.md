# PQ AI Terminal — 基于终端波形数据的电能质量 AI 应用

> **项目全称**：基于终端交流采样波形数据的电能质量 AI 应用 —— 新能源与充电桩接入影响评估
> **目标平台**：全志 T536（4×Cortex-A55 + E907 RISC-V） + 钜泉 HT7627S（7 通道 24bit AFE） + 瑞芯微 RK3576（外挂算力模组，USB ECM 互连）
> **版本**：v2.4.0 ｜ **日期**：2026-08-14
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
│   ├── app/                       #   嵌入式真实入口（wave_export_arm.c / wave_sender_arm.c / wave_inference_server_v2.py / wave_inference_server_v5_npu.py）
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
│                          │                                            │
│                          ▼ (异常触发)                                  │
│                   ┌──────────────────────────────┐                     │
│                   │   RKLLM 大模型服务           │                     │
│                   │   (qwen3-1.7b-rk3576)        │                     │
│                   │   异常根因分析 + 治理建议     │                     │
│                   └──────────────────────────────┘                     │
└───────────────────────────────────────────────────────────────────────┘
```

**数据流**：
1. T536+HT7627S 采样 → 波形采集 → 原始波形数据
2. 原始波形通过 USB ECM 二进制协议发送给 RK3576（24字节协议头 + 7182字节波形数据）
3. RK3576 解析波形 → 计算 RMS → 提取 27 维特征向量 → 运行 iForest/AE/CNN1D 推理
4. **异常触发 LLM**：if_score > 0.4 时自动调用 RKLLM 进行根因分析
5. RK3576 返回 AI 推理结果 + LLM 分析（含异常解释、故障诊断、治理建议）
6. T536 接收结果 → 场景识别 → MQTT 上报 + 治理建议

**已验证的真实硬件流程**（v2.4.0）：
- T536 A相加压（UA RMS≈235V），B/C相开路（UB/UC RMS≈1.2V）
- 完整业务链路：T536采集→TCP发送→RK3576解析→特征提取→NPU推理→AI响应
- AI推理结果正确：CNN=7（three_loss），置信度0.92
- RKLLM 与 RKNN 模型共存验证通过：Core 0 (RKLLM) + Core 1 (RKNN NPU)

**AI 响应扩展**（v2.4.0）：
- 响应格式从 35 字节扩展为 63 字节
- 新增 7 通道有效值：UA/UB/UC/IA/IB/IC/IZ
- 格式：`<IBQIfffBBfffffffI` (17项)
- CRC32 校验：基于 Payload 前 59 字节

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

### v2.4.0 RK3576 NPU 推理全链路验证

| 环节 | 状态 | 说明 |
|------|------|------|
| RKNN 模型部署 | ✅ | cnn1d_8class.rknn (1.4MB) |
| NPU 推理服务 | ✅ | wave_inference_server_v5_npu.py |
| V2 协议通信 | ✅ | 7194字节波形帧，CRC32校验 |
| AI 响应解析 | ✅ | 63字节扩展格式，7通道有效值 |
| RKLLM 共存 | ✅ | Core 0 (RKLLM) + Core 1 (RKNN NPU) |
| 推理性能 | ✅ | 2-3ms/周期，3/3成功 |

**NPU 推理结果**（A相加压，B/C相开路）：
```
周期 1: UA=235.273V, UB=1.200V, UC=1.188V → three_loss (0.92)
周期 2: UA=235.316V, UB=1.189V, UC=1.181V → three_loss (0.92)
周期 3: UA=235.333V, UB=1.176V, UC=1.165V → three_loss (0.92)
```

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

## AI 推理模式说明

### 推理模式对比

系统支持两种 AI 推理模式，可根据实际需求选择：

| 特性 | Heuristic 模式（推荐） | NPU 模式（实验性） |
|------|----------------------|-------------------|
| **工作原理** | 基于物理规则的确定性计算 | RKNN 神经网络推理 |
| **输入** | 27 维特征向量 | 特征向量 + 原始波形 |
| **响应速度** | < 0.01 ms | ~0.5 ms (NPU) / ~6 ms (CPU) |
| **稳定性** | ✅ 稳定可预测 | ⚠️ 依赖模型训练 |
| **适用阶段** | 当前生产使用 | 模型训练完成后 |
| **异常检测** | 多指标综合检测 (CV+电压偏差+不平衡度) | iForest 模型 |
| **事件分类** | 阈值判定规则 | CNN1D 模型 |
| **LLM 集成** | ✅ 支持 (异常时调用 RKLLM) | ✅ 支持 |

### RKLLM 大模型集成

RK3576 上已部署 RKLLM 大模型服务，AI 推理服务检测到异常时自动调用 RKLLM 进行根因分析：

| 组件 | 配置 |
|------|------|
| **模型** | qwen3-1.7b-rk3576.rkllm (1.5GB) |
| **服务端口** | 127.0.0.1:8080 |
| **推理模式** | RKNN NPU 加速 |
| **触发条件** | IF > 0.4 或 CNN 异常分类 或 AE > 100 |
| **响应内容** | 异常解释、故障诊断、治理建议 |
| **响应时间** | ~30-95s (首次加载较慢) |

**LLM 调用日志示例**：
```
[异常检测] seq=1, IF=0.8000, CNN=3(谐波), 置信度=0.9000, 场景=S3
[异常检测] 启动LLM根因分析 seq=1
[ai.llm_advisor] 检测到异常, 调用 LLM 辅助决策...
[LLM决策] seq=1, 分析耗时=95875ms, 
          评估={"severity": "high", "summary": "AI判定: 异常 (iForest=0.80)", 
          "has_llm_support": true}
```

### 切换方式

```bash
# 命令行参数
python integrated_inference_service.py --inference-mode heuristic  # 默认，稳定可靠
python integrated_inference_service.py --inference-mode npu        # 实验性，需模型训练

# 或修改 config.ini
[ai]
inference_mode = heuristic  # 改为 npu 启用 NPU 模式
```

### Heuristic 模式计算逻辑 (v2.3.1 改进版)

Heuristic 模式基于物理启发式规则进行多指标综合检测：

1. **变异系数分数 (cv_score)**
   - `cv_score = min(cv * 2.0, 1.0)` — 放大系数使 0.3 CV 变为 0.6
   
2. **电压偏离检测 (deviation_score)**
   - 正常范围: 220V ± 20% (176V - 264V)
   - `deviation_score = max_deviation * 3.0`
   
3. **三相不平衡度检测 (unbalance_score)**
   - `unbalance_score = unbalance_pct / 50.0`
   
4. **综合 IF 分数**
   - `if_score = max(cv_score, deviation_score * 0.8, unbalance_score * 0.6)`

5. **异常判定条件 (满足任一即触发)**
   - `if_score > 0.4` 或 `cnn_class > 0` 或 `ae_score > 100`

6. **CNN 分类**
   - IF > 0.6: class=3 (严重异常, 谐波)
   - IF > 0.4: class=2 (中度异常)
   - IF > 0.25: class=1 (轻度异常)
   - 其他: class=0 (正常)

---

## 手动测试指南

### RK3576 上手动测试

```bash
# Step 1: SSH 连接 RK3576
ssh cat@192.168.100.1          # USB ECM
# 或
ssh cat@192.168.137.204       # 有线

# Step 2: 进入项目目录
cd /home/cat/pq_ai_v3

# Step 3a: 单次推理测试 (Heuristic 模式)
python3 app/integrated_inference_service.py --inference-mode heuristic --once

# Step 3b: 单次推理测试 (NPU 模式, 实验性)
python3 app/integrated_inference_service.py --inference-mode npu --once

# Step 4: 批量测试 (100 次循环)
python3 app/integrated_inference_service.py --inference-mode heuristic --mode test --cycles 100

# Step 5: 启动 AI 服务 (等待 T536 连接)
# 使用管理脚本 (推荐)
./scripts/rk3576_ai_service.sh start

# 或手动启动
python3 -u app/integrated_inference_service.py \
    --device rk3576 \
    --inference-mode heuristic \
    --host 192.168.100.1 \
    --port 9090

# Step 5: 查看服务状态和日志
./scripts/rk3576_ai_service.sh status   # 查看状态
./scripts/rk3576_ai_service.sh logs     # 查看最近日志
tail -f logs/ai_server_*.log            # 实时查看

# Step 6: 验证 RKLLM 连通性 (可选)
python3 test_rkllm_connectivity.py      # 测试 RKLLM 服务

# Step 7: 停止服务
./scripts/rk3576_ai_service.sh stop
```

### T536 上手动测试

```bash
# Step 1: SSH 连接 T536
ssh -p 8888 csg@192.168.14.101
# 或
ssh -p 8888 csg@192.168.100.2

# Step 2: 检查程序和依赖
cd /home/csg/wave_sender_test
ls -la wave_sender_arm                   # 检查程序
ls -la /lib32/ld-linux-armhf.so.3        # 检查动态链接器
ping -c 3 192.168.100.1                  # 测试网络

# Step 3: 单次波形发送测试
# 使用管理脚本 (推荐)
CYCLES=1 ./scripts/t536_wave_service.sh start

# 或手动运行 (10 次循环)
/lib32/ld-linux-armhf.so.3 \
    --library-path /lib32:/custom/sys/lib/hal_lib/lib32 \
    ./wave_sender_arm \
    --server 192.168.100.1 \
    --port 9090 \
    --cycles 10 \
    --interval 200

# Step 4: 持续运行模式
./scripts/t536_wave_service.sh start

# 或手动运行 (持续运行, 5Hz)
/lib32/ld-linux-armhf.so.3 \
    --library-path /lib32:/custom/sys/lib/hal_lib/lib32 \
    ./wave_sender_arm \
    --server 192.168.100.1 \
    --port 9090 \
    --interval 200

# Step 5: 查看日志
./scripts/t536_wave_service.sh status    # 状态
./scripts/t536_wave_service.sh logs      # 日志
tail -f wave_sender_*.log                # 实时日志

# Step 6: 停止服务
./scripts/t536_wave_service.sh stop
```

### 完整联调测试流程

```
时序要求: 先启动 RK3576，再启动 T536
```

```bash
# Step 1: RK3576 启动 AI 服务
ssh cat@192.168.100.1
cd /home/cat/pq_ai_v3
./scripts/rk3576_ai_service.sh start
# 预期: [运行中] [监听中] 192.168.100.1:9090

# Step 2: T536 启动波形发送 (另一个终端)
ssh -p 8888 csg@192.168.14.101
cd /home/csg/wave_sender_test
./scripts/t536_wave_service.sh start
# 预期: [运行中] 连接 RK3576 成功

# Step 3: RK3576 查看接收日志
./scripts/rk3576_ai_service.sh logs
# 预期: 客户端连接 → 接收波形 → 特征提取 → 推理 → 响应

# Step 4: 停止服务 (先 T536 后 RK3576)
# T536 端
./scripts/t536_wave_service.sh stop
# RK3576 端
./scripts/rk3576_ai_service.sh stop
```

### 快捷命令汇总

```bash
# RK3576 快捷启动
ssh cat@192.168.100.1 'cd /home/cat/pq_ai_v3 && ./scripts/rk3576_ai_service.sh start'

# T536 快捷启动
ssh -p 8888 csg@192.168.14.101 'cd /home/csg/wave_sender_test && ./scripts/t536_wave_service.sh start'

# RK3576 查看状态/日志
ssh cat@192.168.100.1 'cd /home/cat/pq_ai_v3 && ./scripts/rk3576_ai_service.sh status'
ssh cat@192.168.100.1 'cd /home/cat/pq_ai_v3 && ./scripts/rk3576_ai_service.sh logs 20'

# RKLLM 服务管理
ssh cat@192.168.100.1 'cd /home/cat/pq_ai_v3 && ./scripts/rk3576_ai_service.sh rkllm-status'
ssh cat@192.168.100.1 'cd /home/cat/pq_ai_v3 && ./scripts/rk3576_ai_service.sh rkllm-restart'

# 一键停止所有服务
ssh -p 8888 csg@192.168.14.101 'cd /home/csg/wave_sender_test && ./scripts/t536_wave_service.sh stop'
ssh cat@192.168.100.1 'cd /home/cat/pq_ai_v3 && ./scripts/rk3576_ai_service.sh stop'
```

### RKLLM 大模型服务管理

RK3576 AI 服务管理脚本已集成 RKLLM 服务管理：

```bash
# 查看 RKLLM 状态
./scripts/rk3576_ai_service.sh rkllm-status
# 预期: RKLLM 服务运行中 (pid: 12345, 端口: 8080)

# 重启 RKLLM 服务
./scripts/rk3576_ai_service.sh rkllm-restart

# 查看 RKLLM 日志
./scripts/rk3576_ai_service.sh rkllm-logs

# 测试 RKLLM 连通性
python3 test_rkllm_connectivity.py
# 测试内容: 健康检查、模型列表、聊天补全、流式输出
```

**RKLLM 服务启动顺序**：
```
mosquitto (MQTT broker)
    ↓
rkllm-server (大模型推理服务, 端口 8080)
    ↓
rkllm-mqtt-bridge (MQTT 桥接)
    ↓
AI 推理服务 (本项目, 端口 9090)
```

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

## 技术实现说明

### T536 (ARM32) ↔ RK3576 (ARM64) 字节序处理

#### 背景

T536 是 ARM 32 位架构，RK3576 是 ARM 64 位架构，两者通过 USB ECM 虚拟网卡进行 TCP 通信。正确处理字节序和数据类型是确保跨平台数据传输正确性的关键。

#### 核心原则

1. **统一使用 Little-Endian**：T536 (ARM32) 和 RK3576 (ARM64) 都是 Little-Endian 架构
2. **使用 `<stdint.h>` 标准类型**：`uint32_t`, `uint64_t` 确保类型大小一致
3. **手动解析而非直接 memcpy**：避免结构体在不同架构上的对齐差异
4. **CRC32 基于原始字节计算**：不依赖结构体内存布局

#### AI 响应结构 (35 字节)

```c
// C 端定义 (使用 __attribute__((packed))
typedef struct __attribute__((packed)) {
    uint32_t magic;         // offset 0,  size 4
    uint8_t  resp_type;     // offset 4,  size 1
    uint64_t timestamp;     // offset 5,  size 8  ← 关键点: 64位时间戳
    uint32_t cycle_seq;     // offset 13, size 4
    float    if_score;      // offset 17, size 4
    float    ae_score;      // offset 21, size 4
    float    cnn_confidence;// offset 25, size 4
    uint8_t  cnn_class;     // offset 29, size 1
    uint8_t  scene_id;      // offset 30, size 1
    uint32_t crc32;         // offset 31, size 4
} ai_response_v2_t;
```

#### Python 端打包 (RK3576)

```python
# 使用 struct.pack 明确指定小端格式
resp_data = b''.join([
    struct.pack('<I', magic),           # uint32_t → 4 bytes
    struct.pack('<B', resp_type),       # uint8_t  → 1 byte
    struct.pack('<Q', timestamp),       # uint64_t → 8 bytes  ← 64位整数
    struct.pack('<I', cycle_seq),       # uint32_t → 4 bytes
    struct.pack('<f', if_score),        # float    → 4 bytes
    struct.pack('<f', ae_score),        # float    → 4 bytes
    struct.pack('<f', cnn_confidence),  # float    → 4 bytes
    struct.pack('<B', cnn_class),       # uint8_t  → 1 byte
    struct.pack('<B', scene_id),        # uint8_t  → 1 byte
])
# CRC32 基于前 31 字节计算
crc32_val = zlib.crc32(resp_data) & 0xFFFFFFFF
resp_data += struct.pack('<I', crc32_val)
```

#### C 端解析 (T536)

```c
// 添加 64 位小端读取函数
static uint64_t read_le64(const uint8_t *p) {
    return ((uint64_t)p[0]) |
           ((uint64_t)p[1] << 8) |
           ((uint64_t)p[2] << 16) |
           ((uint64_t)p[3] << 24) |
           ((uint64_t)p[4] << 32) |
           ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) |
           ((uint64_t)p[7] << 56);
}

// 逐字段解析 (避免直接 memcpy)
void parse_ai_response(const uint8_t *payload_ptr, ai_response_v2_t *result) {
    uint8_t *p = payload_ptr;
    result->magic = read_le32(p); p += 4;
    result->resp_type = *p++;
    result->timestamp = read_le64(p); p += 8;  // 64位时间戳
    result->cycle_seq = read_le32(p); p += 4;
    memcpy(&result->if_score, p, 4); p += 4;   // float 可直接 memcpy
    memcpy(&result->ae_score, p, 4); p += 4;
    memcpy(&result->cnn_confidence, p, 4); p += 4;
    result->cnn_class = *p++;
    result->scene_id = *p++;
    result->crc32 = read_le32(p); p += 4;
}
```

#### CRC32 实现

```c
// 与 zlib.crc32 兼容, 使用多项式 0xEDB88320
static uint32_t crc32_calc(const uint8_t *data, int len) {
    uint32_t crc = 0xFFFFFFFF;
    for (int i = 0; i < len; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    }
    return crc ^ 0xFFFFFFFF;
}
```

#### 验证结果

| 测试项 | 结果 |
|--------|------|
| Python 打包大小 | 35 字节 ✓ |
| C 端解析 | 所有 10 个字段正确 ✓ |
| CRC32 校验 | 发送端和接收端一致 ✓ |
| 5 轮循环 | 100% 成功率 ✓ |

---

### 服务端重连机制

#### 问题描述

原来的服务端实现中，客户端断开连接后无法重新建立连接。原因是 `accept()` 接受连接后，监听 socket 被客户端 socket 覆盖，导致后续无法继续监听新的客户端连接。

#### 修复方案

**1. 保存监听 socket**

```python
class ReliableTransport:
    def __init__(self, ...):
        self._sock = None        # 当前连接的客户端 socket
        self._listen_sock = None # 保存监听 socket (新增)
```

**2. 服务端 accept 流程**

```python
def connect(self):
    if self.is_server:
        # 创建并保存监听 socket
        self._listen_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._listen_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._listen_sock.bind((self.host, self.port))
        self._listen_sock.listen(128)
        
        # accept 返回客户端 socket, 不覆盖 _listen_sock
        self._sock, client_addr = self._listen_sock.accept()
```

**3. 添加 reaccept() 方法**

```python
def reaccept(self) -> bool:
    """客户端断开后, 重新接受新的客户端连接"""
    if not self.is_server or not self._listen_sock:
        return False
    
    # 复用保存的监听 socket
    while self._running:
        try:
            self._sock, client_addr = self._listen_sock.accept()
            log.info(f"客户端重连: {client_addr}")
            break
        except socket.timeout:
            continue
    
    self._connected = True
    self._start_background_threads()
    return True
```

**4. 修改 accept_loop**

```python
def _accept_loop(self):
    while self._server_running:
        # 首次连接使用 connect(), 后续使用 reaccept()
        if not self._transport._connected and self._transport._listen_sock:
            if not self._transport.reaccept():
                continue
        elif not self._transport._connected:
            if not self._transport.connect():
                continue
        
        # 等待客户端断开
        while self._transport.is_connected and self._server_running:
            time.sleep(0.5)
        
        # 客户端断开, 准备重新监听
        self._transport.disconnect()
```

#### 验证结果

```
# RK3576 AI 服务日志
18:50:18 [I] 客户端连接: ('192.168.100.2', 39640)    # 第 1 次连接
18:50:19 [I] 客户端重连: ('192.168.100.2', 39654)    # 第 2 次连接
18:50:21 [I] 客户端重连: ('192.168.100.2', 39656)    # 第 3 次连接
18:50:22 [I] 客户端重连: ('192.168.100.2', 39664)    # 第 4 次连接
18:50:24 [I] 客户端重连: ('192.168.100.2', 39668)    # 第 5 次连接
```

| 测试项 | 结果 |
|--------|------|
| 5 轮循环 | 全部成功 ✓ |
| 重连机制 | 正常工作 ✓ |
| 数据传输 | 100% 成功率 ✓ |

---

### 修复文件清单

| 文件 | 修改内容 |
|------|----------|
| `pq_ai_terminal/app/wave_sender_arm_test.c` | 添加 `read_le64()` 函数，修改 AI 响应解析为逐字段方式 |
| `pq_ai_terminal/app/wave_sender_arm.c` | 添加 `read_le64()` 函数，修正 CRC32 实现，修改 AI 响应解析 |
| `pq_ai_terminal/comm/protocol_v2.py` | 添加 `_listen_sock` 保存监听 socket，添加 `reaccept()` 方法，修改 `_accept_loop()` |

---

## 文档

- [项目开发手册（完整复现版）](pq_ai_terminal/docs/项目开发手册（完整复现版）.md) — 新 PC 环境复现、新人培训、维护指南
- [RK3576 NPU 模型训练与部署技术文档](pq_ai_terminal/docs/RK3576_NPU模型训练与部署技术文档.md) — NPU 模型部署全流程
- [波形数据格式与通信协议详解](pq_ai_terminal/docs/波形数据格式与通信协议详解.md) — V2 协议规范
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

### v2.4.0 (2026-08-14) — RK3576 NPU 推理部署与 AI 响应扩展版

- **RK3576 NPU 推理部署**：
  - 部署 RKNN 模型 `cnn1d_8class.rknn` 到 RK3576
  - 实现 `wave_inference_server_v5_npu.py` 基于 RKNN Toolkit Lite2 的 NPU 推理服务
  - NPU Core 1 与 RKLLM Core 0 隔离，实现多模型共存
  - 推理延迟 2-3ms，精度 95.8%

- **AI 响应格式扩展**：
  - 响应格式从 35 字节扩展为 63 字节
  - 新增 7 通道有效值字段：UA/UB/UC/IA/IB/IC/IZ
  - 格式：`<IBQIfffBBfffffffI` (17项)
  - CRC32 校验：基于 Payload 前 59 字节
  - 更新 C 端 `wave_sender_arm.c` 解析逻辑

- **通信协议更新**：
  - 更新 `docs/波形数据格式与通信协议详解.md` 至 v1.2
  - 新增 CNN 分类 class 7 (three_loss)
  - 完善 V2 协议帧格式说明

- **交叉编译流程固化**：
  - 使用 GCC Linaro 5.3.1 编译 32 位 ARM 程序
  - 部署流程：交叉编译服务器 → SCP 上传 → T536 解压部署

- **真实硬件验证**：
  - T536 A相加压(UA RMS≈235V), B/C相开路(UB/UC≈1.2V)
  - AI 推理：CNN=7 (three_loss), 置信度 0.92
  - 3/3 周期 100% 成功率
  - RKLLM + RKNN 双模型共存验证通过

- **新增技术文档**：
  - `docs/RK3576_NPU模型训练与部署技术文档.md`：NPU 模型训练与部署全流程
  - `docs/波形数据格式与通信协议详解.md`：V2 协议规范 v1.2

### v2.3.1 (2026-08-13) — RKLLM 集成与异常检测优化版

- **RKLLM 大模型集成**：
  - RK3576 上部署 qwen3-1.7b-rk3576 大模型服务
  - AI 推理服务检测到异常时自动调用 RKLLM 进行根因分析
  - 异常触发条件：IF > 0.4 或 CNN 异常分类 或 AE > 100
  - LLM 返回异常解释、故障诊断、治理建议
  - 响应时间 ~30-95s (首次加载较慢)

- **异常检测逻辑优化**：
  - Heuristic 模式新增多指标综合检测
  - 变异系数放大系数：cv_score = min(cv * 2.0, 1.0)
  - 电压偏离检测：基于 220V ± 20% 范围
  - 三相不平衡度检测：unbalance_score = unbalance_pct / 50.0
  - 综合 IF 分数：取三项最大值
  - 降低异常判定阈值，支持多种异常类型检测

- **服务管理脚本增强**：
  - rk3576_ai_service.sh 集成 RKLLM 服务管理
  - 新增 rkllm-status、rkllm-restart、rkllm-logs 命令
  - 启动顺序：mosquitto → rkllm-server → rkllm-mqtt-bridge → AI 服务

- **新增测试工具**：
  - test_rkllm_connectivity.py：验证 RKLLM 服务连通性
  - 测试内容：健康检查、模型列表、聊天补全、流式输出

- **真实硬件验证**：
  - T536 A相加压 B/C相开路场景
  - AI 推理：IF=0.8000 (异常), CNN=3 (严重异常), 置信度 0.90
  - LLM 调用成功：耗时 95.88s, 评估结果 "high"

### v2.3.0 (2026-08-13) — 字节序、重连机制与推理模式修复版

- **AI 推理模式重构**：
  - 新增 `InferenceMode` 枚举，支持 `heuristic`（物理启发式）和 `npu`（RKNN 神经网络）两种模式
  - Heuristic 模式：基于物理规则的确定性计算，使用电压变异系数检测异常，稳定可靠
  - NPU 模式：RKNN 神经网络推理，需模型训练完成后使用
  - 添加 `--inference-mode` 命令行参数，支持运行时切换
  - 添加 `_validate_npu_outputs()` 方法验证 NPU 输出有效性（NaN/Inf/全零检测）
  - NPU 模式自动回退机制：检测到无效结果时自动切换到 heuristic 模式
  - 更新 `config.ini` 添加 `inference_mode` 配置项

- **字节序处理**：
  - T536 (ARM32) 和 RK3576 (ARM64) 数据传输字节序统一为 Little-Endian
  - 添加 `read_le64()` 函数处理 64 位小端整数时间戳
  - 使用 `<stdint.h>` 标准类型（uint32_t、uint64_t）确保类型大小一致
  - AI 响应结构解析从直接 memcpy 改为逐字段解析，避免结构体对齐差异
  - CRC32 实现统一为多项式 0xEDB88320，右移操作，与 Python `zlib.crc32` 兼容
  - 帧格式从 `[header][payload][crc]` 改为 `[header][crc][payload]`，与服务端解析一致

- **服务端重连机制**：
  - 添加 `_listen_sock` 保存监听 socket，避免被客户端 socket 覆盖
  - 添加 `reaccept()` 方法，支持客户端断开后重新接受连接
  - 修改 `_accept_loop()`，首次连接使用 connect()，后续使用 reaccept()
  - 5 轮循环测试验证，重连机制正常工作，数据传输成功率 100%

- **文档更新**：
  - README.md 添加"技术实现说明"章节，详细描述字节序处理和重连机制
  - 添加修复文件清单表格

### v2.2.1 (2026-08-12) — 密码修正与运维文档增强

- **更正 T536 SSH 密码**：从旧密码修正为 `Iot@csg123`
  - 涉及文件：`config.ini`、`项目开发手册`、`deploy_and_test.sh`、`generate_tech_report.py`
- **新增 Q16 FAQ**：T536 SSH 账户被锁定（`pam_tally2` 解锁方法）
- **排查并修复**：T536 `csg` 账户因 21 次失败登录被 PAM 锁定
- **生成对外技术方案报告 Word 版**：`PQ_AI_Terminal_技术方案报告_v2.2.0.docx`

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
- **文档版本**：v2.4.0（2026-08-14）
- **版本权威源**：[pq_ai_terminal/include/pq_version.h](pq_ai_terminal/include/pq_version.h)
- **GitHub**：[https://github.com/chihinglau/pq_ai](https://github.com/chihinglau/pq_ai)
