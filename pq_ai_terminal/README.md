# PQ AI Terminal — T536 + HT7627S + RK3576 嵌入式软件工程

> **版本**：v2.1.1 双机协作架构版（诊断增强）
> **日期**：2026-08-03
> **权威源**：[include/pq_version.h](include/pq_version.h)

## 项目概述

本项目是基于 **双机协作架构** 的电能质量 AI 终端完整嵌入式软件工程：

- **采样主机**：全志 **T536**（4× Cortex-A55 + E907 RISC-V，**不带 NPU**）+ 钜泉 **HT7627S**（7 通道高精度计量 AFE）。
- **算力模组**：瑞芯微 **RK3576**（外挂，运行 iForest/AE/CNN1D/大模型 AI 推理）。
- **互连通道**：**USB ECM**（Ethernet Control Model），USB 虚拟网卡 TCP 通信。
- **职责划分**：T536+HT7627S 负责采样、PQ 指标计算、事件触发、特征提取、MQTT 上报；RK3576 负责 AI 推理。

**当前状态**：三套验证环境已搭建完毕（MATLAB 仿真 / Windows MinGW / WSL Ubuntu 26.04），可在无真实硬件条件下运行全功能双机协作演示。

## 双机协作架构

```
┌─────────────── 采样主机 T536 + HT7627S ──────────────┐
│  HT7627S → HAL → PQ指标 → 事件触发 → 特征提取        │
│                                          │            │
│                            AI RPC 客户端 ◄┘            │
└──────────────────────────┬─────────────────────────────┘
                           │   USB ECM 虚拟网卡
                           │   (TCP over USB)
┌──────────────────────────▼─────────────────────────────┐
│              算力模组 RK3576                            │
│   AI 推理服务 (监听 9090)                               │
│     ├─ iForest 异常检测                                │
│     ├─ AE 重构误差                                      │
│     └─ 1D-CNN 事件分类                                 │
└─────────────────────────────────────────────────────────┘
```

## 工程目录结构

```
pq_ai_terminal/
├── Makefile                    # 纯 Makefile，跨平台（Windows MinGW / Linux GCC / aarch64 交叉编译）
├── CMakeLists.txt              # 顶层 CMake 配置（可选）
├── config.ini                  # 运行时配置（含 [compute_module] 算力模组配置段）
├── include/                    # 公共头文件
│   ├── pq_common.h             #   类型别名、平台宏、调试宏
│   ├── pq_hal.h                #   HAL 接口（寄存器/波形结构体）
│   ├── pq_config.h             #   INI 配置解析器接口
│   └── pq_version.h            #   版本信息（权威源）
├── drivers/                    # 驱动层
│   └── ht7627s_regs.h          #   HT7627S 寄存器地址宏定义
├── core/                       # 核心算法层（主机侧运行）
│   ├── pq_metrics.h/.c         #   12 项 PQ 指标计算（GB/T 国标）
│   ├── event_trigger.h/.c      #   事件触发引擎（滞回/严重度）
│   ├── wave_freeze.h/.c        #   波形冻结与环形缓冲
│   ├── feature_extract.h/.c    #   27 维特征工程
│   └── scenario_detect.h/.c    #   S1~S5 场景识别
├── ai/                         # AI 推理层
│   ├── iforest_infer.h/.c      #   孤立森林异常检测（算力模组运行）
│   ├── ae_infer.h/.c           #   自编码器异常检测（算力模组运行）
│   ├── cnn1d_infer.h/.c        #   1D-CNN 事件分类（算力模组运行）
│   └── ai_rpc.h/.c             #   AI RPC 客户端（主机侧，USB ECM 通信，带本地 fallback）
├── comm/                       # 通信层
│   ├── proto_mqtt.h/.c         #   MQTT 客户端 Stub
│   ├── time_sync.h/.c          #   时间同步 Stub
│   └── usb_ecm.h/.c            #   USB ECM 传输层（跨平台 TCP socket）
├── utils/                      # 工具层
│   ├── ring_buffer.h/.c        #   无锁环形缓冲
│   ├── json_builder.h/.c       #   轻量级 JSON 构造器
│   ├── sqlite_wrapper.h/.c     #   数据持久化（CSV 模拟）
│   └── pq_config.c             #   INI 配置文件解析实现
├── sim/                        # 软模拟仿真层
│   ├── hal_sim.h/.c            #   HT7627S 软件模拟器（T536 仿真）
│   ├── compute_module_sim.h/.c #   RK3576 算力模组仿真器（后台 TCP 服务线程）
│   └── sim_main.c              #   仿真主程序（Windows/Linux 通用）
├── app/                        # 嵌入式真实入口
│   └── main.c                  #   RTOS/Linux 主入口（真实硬件就绪后启用）
├── scripts/                    # 辅助脚本
│   ├── build_win.bat           #   Windows 一键构建
│   ├── build_linux.sh          #   Linux 交叉编译脚本
│   └── run_all.bat             #   全场景批量运行
├── cmake/                      # 交叉编译工具链文件
│   └── aarch64-linux-gnu.cmake
└── docs/                       # 项目文档
    ├── 项目开发手册.md
    ├── 项目开发手册（完整复现版）.md   # 核心开发文档（v2.1.0）
    └── Linux环境技术方案.md           # Linux 环境技术方案（含 USB ECM 双机架构）
```

## 核心模块说明

### 主机侧（T536 + HT7627S）

| 模块 | 文件 | 功能 |
|------|------|------|
| HAL 仿真层 | [sim/hal_sim.c](sim/hal_sim.c) | 模拟 HT7627S ADC 采样、谐波计算、功率计量；跨平台（Windows `Sleep`/Linux `poll`） |
| PQ 指标计算 | [core/pq_metrics.c](core/pq_metrics.c) | 12 项国标 PQ 指标（电压偏差/THD/不平衡度/频率偏差/负载率等） |
| 事件触发引擎 | [core/event_trigger.c](core/event_trigger.c) | 7 类事件检测（暂降/暂升/谐波/不平衡/过载/频率/场景），滞回机制 |
| 波形冻结 | [core/wave_freeze.c](core/wave_freeze.c) | 事件触发前后波形环形缓冲与冻结 |
| 特征提取 | [core/feature_extract.c](core/feature_extract.c) | 27 维特征向量（16 项标准 + 11 项波形特征） |
| 场景识别 | [core/scenario_detect.c](core/scenario_detect.c) | S1~S5 五类典型场景自动识别 |
| AI RPC 客户端 | [ai/ai_rpc.c](ai/ai_rpc.c) | 通过 USB ECM 调用算力模组 AI 推理，带本地 fallback |
| MQTT 上报 | [comm/proto_mqtt.c](comm/proto_mqtt.c) | 指标/事件/AI 结果上报云端 |
| 数据持久化 | [utils/sqlite_wrapper.c](utils/sqlite_wrapper.c) | CSV/SQLite 本地存储 |

### 通信层（USB ECM 双机互连）

| 模块 | 文件 | 功能 |
|------|------|------|
| USB ECM 传输层 | [comm/usb_ecm.c](comm/usb_ecm.c) | 跨平台 TCP socket 封装（Windows Winsock2 / Linux BSD socket），`intptr_t` 统一句柄 |
| 算力模组仿真器 | [sim/compute_module_sim.c](sim/compute_module_sim.c) | RK3576 仿真，后台 TCP 服务线程，监听 9090，运行 iForest/AE/CNN1D 本地 Stub |

### 算力模组侧（RK3576，仿真器模拟）

| 模块 | 文件 | 功能 |
|------|------|------|
| 孤立森林 | [ai/iforest_infer.c](ai/iforest_infer.c) | 32 棵随机树，深度 8，输出异常得分 [0,1] |
| 自编码器 | [ai/ae_infer.c](ai/ae_infer.c) | 27→8→27 结构，tanh 激活，输出重构误差 MSE |
| 1D-CNN | [ai/cnn1d_infer.c](ai/cnn1d_infer.c) | 8 滤波器×5 核卷积，ReLU+全局平均池化，7 类事件分类 |

## 编译与运行

### 前置要求

- **Windows**：MSYS2 + MinGW-w64 GCC（链接 `-lws2_32 -lwinmm`）
- **Linux / WSL**：`build-essential`（GCC 15+，链接 `-lm -lpthread`）
- **交叉编译**：`gcc-aarch64-linux-gnu`（部署到 T536/RK3576）

### 方法一：Makefile（推荐，跨平台自动检测）

```bash
# 进入工程目录
cd d:\ai\prj\trae\pq_ai\pq_ai_terminal     # Windows
cd ~/pq_ai/pq_ai_terminal                    # Linux/WSL

# 编译（Makefile 会自动检测操作系统）
make clean
make sim

# 运行（产物名自动带 .exe 后缀于 Windows）
./pq_sim --help
./pq_sim --scenario S4 --cycles 100
./pq_sim --all --cycles 20
```

### 方法二：CMake + Ninja（可选）

```powershell
cmake -S . -B build -G "Ninja" -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Release
ninja -C build
.\build\sim\pq_sim.exe --scenario S4 --cycles 100
```

### 方法三：交叉编译部署到 T536

```bash
sudo apt install gcc-aarch64-linux-gnu
mkdir -p build-linux && cd build-linux
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/aarch64-linux-gnu.cmake -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
file pq_sim   # pq_sim: ELF 64-bit LSB executable, ARM aarch64
```

## 仿真程序用法

```
pq_sim [options]
Options:
  --scenario S1|S2|S3|S4|S5  Select simulation scenario (default: S4)
  --cycles N                 Number of cycles to simulate (default: 100)
  --all                      Run all S1~S5 scenarios sequentially
  -h, --help                 Show this help message

Scenarios:
  S1  Baseline load (340kW industrial)
  S2  EV charging (80kW, 5/7/11/13 harmonics)
  S3  Distributed PV (200kW, voltage rise +2.93%)
  S4  EV+PV coupled (280kW, combined effects)
  S5  Extreme scenario (360kW, high THD)
```

## 运行时配置

关键配置段 `config.ini`：

```ini
[compute_module]
; RK3576 算力模组配置
; enabled=1 时启动本地仿真器模拟 RK3576 行为
; 真实部署时 enabled=0，ip 设为 USB ECM 虚拟网卡地址（如 169.254.1.2）
enabled = 1
ip = 127.0.0.1
port = 9090
```

| 参数 | 仿真模式取值 | 真实部署取值 | 说明 |
|------|--------------|--------------|------|
| `enabled` | `1` | `0` | 是否启动本地算力模组仿真器 |
| `ip` | `127.0.0.1` | `169.254.1.2` | 算力模组 IP |
| `port` | `9090` | `9090` | AI 推理服务监听端口 |

## 双机协作流程验证

运行仿真后，预期输出：

```
[INIT] RK3576 compute module simulator started (127.0.0.1:9090)
[INIT] AI RPC client initialized (target: 127.0.0.1:9090)
[INIT] HAL initialized, sample_rate=12800, channels=7

--- Cycle 0 (scenario=S4-光充耦合) ---
  Voltage Deviation       +2.930 %
  Voltage THD                5.000 %
  Current THD               16.400 %
  IF Anomaly Score           0.420
  AE Anomaly Score           1250000.000
  CNN Event Class            3 (conf=0.870)
  AI Compute Module          ONLINE (RK3576 via USB ECM)
```

**验证项**：

| 项目 | 预期结果 | 判定方式 |
|------|----------|----------|
| 算力模组仿真器启动 | 监听 9090 端口 | `ss -tlnp \| grep 9090` |
| USB ECM 连接建立 | 主机连接成功 | 日志 `ONLINE (RK3576 via USB ECM)` |
| AI 推理结果返回 | if_score/ae_score/cnn_class 非零 | 周期打印输出 |
| 通信稳定性 | 100 周期全部 ONLINE | 最终摘要统计 |
| 降级机制 | 不可达时切换本地 | `enabled=0` 测试 |

## 输出与数据

运行后会在当前目录生成：

- `pq_metrics.csv`：每周期 PQ 指标记录
- `pq_events.csv`：触发事件记录
- 控制台输出：实时指标、事件告警、场景识别结果、AI 推理结果、USB ECM 通信状态

## 跨平台支持

| 平台 | 编译宏 | 链接库 | 可执行格式 |
|------|--------|--------|------------|
| Windows MinGW | `PLATFORM_WINDOWS` | `-lwinmm -lws2_32` | PE (.exe) |
| Linux / WSL | `PLATFORM_LINUX` | `-lm -lpthread` | ELF |
| aarch64 交叉编译 | `PLATFORM_LINUX` | `-lm -lpthread` | ELF (ARM64) |

**关键跨平台设计**：
- `usb_ecm.c` 统一封装 Winsock2 与 BSD socket，`intptr_t` 统一 socket 句柄类型。
- `hal_sim.c` 使用 `poll(NULL, 0, ms)` 替代已废弃的 `usleep`。
- `compute_module_sim.c` 使用 `pthread` 后台线程，顶部定义 `_POSIX_C_SOURCE 200112L` 解决 `nanosleep` 声明。

## 版本管理

版本权威源：[include/pq_version.h](include/pq_version.h)

推送 GitHub 前必须同步刷新以下三处：
1. `include/pq_version.h`（C 源码版本宏）
2. 根级 `README.md`（GitHub 仓库首页）
3. `DOCUMENTATION.md`（开发部署文档）

### 版本历史

- **v2.1.1 (2026-08-03) — 双机协作架构版（诊断增强）**：USB ECM/AI RPC/算力模组仿真器添加详细诊断日志（errno、字节数、往返耗时、各阶段微秒级计时）；新增 USB ECM 通信与 AI 推理模块单元测试（13 项，覆盖正常流程+异常降级+恢复场景）；Makefile 新增 `test` 目标；重构 Linux环境技术方案.md 与 README.md。
- **v2.1.0 (2026-08-02) — 双机协作架构版**：T536 不带 NPU，新增 RK3576 算力模组通过 USB ECM 外挂；新增 `comm/usb_ecm`、`ai/ai_rpc`、`sim/compute_module_sim`；`sim_main.c` 改为 RPC 调用 AI，500 周期全部 ONLINE。
- **v2.0.0 (2026-08-02) — 完整复现版**：WSL Ubuntu 26.04 + GCC 15 部署验证通过；S1~S5 五场景仿真验证；GitHub 仓库初始化与推送。
- **v1.0.0 (2026-08-01) — 初始版本**：基础仿真框架搭建；五类场景定义；MATLAB 时域仿真引擎。

## 后续工作

### 待完成项

1. **真实 HT7627S 驱动**：替换 `sim/hal_sim.c` 为 SPI 驱动 `drivers/ht7627s_drv.c`
2. **RK3576 独立服务程序**：参考 `sim/compute_module_sim.c` 在 RK3576 上独立编译部署
3. **NPU 模型部署**：将训练好的 ONNX 模型 INT8 量化后通过 RK3576 NPU SDK 部署
4. **USB ECM 真实网卡配置**：T536 配置 `169.254.1.1/24`，RK3576 配置 `169.254.1.2/24`
5. **IEC 61850/MQTT 完整协议栈**：替换 Stub 实现，支持 TLS 加密
6. **SQLite 本地存储**：替换 CSV 为嵌入式 SQLite 数据库

### 已知局限

- AI 模型为随机权重 Stub，异常得分为绝对值，仅用于演示流程
- 场景识别规则为硬编码，后续应替换为 AI 分类模型
- 功率因数计算受谐波影响，当前为近似值
- Windows 仿真未模拟线路阻抗的动态电压降

## 相关文档

- [项目开发手册（完整复现版）](docs/项目开发手册（完整复现版）.md) — 核心开发文档（v2.1.0）
- [Linux 环境技术方案](docs/Linux环境技术方案.md) — Linux 环境搭建与 USB ECM 双机架构
- [DOCUMENTATION.md](../DOCUMENTATION.md) — 开发部署文档

## 许可证

本项目为内部技术方案验证工程，仅供项目团队使用。

## 联系

- 项目：基于终端波形数据的电能质量 AI 应用
- 平台：T536 + HT7627S（采样主机）↔ RK3576（算力模组，USB ECM 互连）
- 仓库：https://github.com/chihinglau/pq_ai
- 日期：2026-08-03
