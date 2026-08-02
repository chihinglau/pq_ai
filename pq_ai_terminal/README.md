# PQ AI Terminal - T536 + HT7627S 嵌入式软件工程

## 项目概述

本项目是基于全志 **T536**（4xA55 + 2T NPU + E907 RISC-V）与钜泉 **HT7627S**（7通道高精度计量AFE）的电能质量AI终端完整嵌入式软件工程。

**当前状态**：Windows软模拟仿真环境已搭建完成，可在无真实硬件条件下运行全功能演示。

## 工程目录结构

```
pq_ai_terminal/
├── CMakeLists.txt              # 顶层CMake配置
├── include/
│   ├── pq_common.h             # 公共头文件、宏定义、类型别名
│   └── pq_hal.h                # 硬件抽象层（HAL）接口
├── drivers/
│   ├── CMakeLists.txt
│   └── ht7627s_regs.h          # HT7627S寄存器地址定义
├── core/                       # 核心算法层
│   ├── CMakeLists.txt
│   ├── pq_metrics.h/.c         # PQ指标计算（THD/不平衡度/偏差等）
│   ├── event_trigger.h/.c      # 事件触发引擎（滞回/严重度）
│   ├── wave_freeze.h/.c        # 波形冻结与环形缓冲
│   ├── feature_extract.h/.c    # 特征工程（27维特征向量）
│   └── scenario_detect.h/.c    # 场景识别（S1~S5）
├── ai/                         # AI推理层
│   ├── CMakeLists.txt
│   ├── iforest_infer.h/.c      # 孤立森林异常检测
│   ├── ae_infer.h/.c           # 自编码器异常检测
│   └── cnn1d_infer.h/.c        # 1D-CNN事件分类
├── comm/                       # 通信层
│   ├── CMakeLists.txt
│   ├── proto_mqtt.h/.c         # MQTT客户端stub
│   └── time_sync.h/.c          # 时间同步（NTP/PTP stub）
├── utils/                      # 工具层
│   ├── CMakeLists.txt
│   ├── ring_buffer.h/.c        # 无锁环形缓冲
│   ├── json_builder.h/.c       # 轻量级JSON构造器
│   └── sqlite_wrapper.h/.c     # 数据持久化（CSV模拟）
├── sim/                        # Windows仿真环境
│   ├── CMakeLists.txt
│   ├── hal_sim.h/.c            # HT7627S软件模拟器
│   └── sim_main.c              # 仿真主程序
├── app/                        # 嵌入式主入口
│   ├── CMakeLists.txt
│   └── main.c                  # 真实板卡入口（RTOS框架）
├── scripts/
│   ├── build_win.bat           # Windows构建脚本
│   └── build_linux.sh          # Linux交叉编译脚本
└── cmake/
    └── aarch64-linux-gnu.cmake # 交叉编译工具链模板
```

## 编译与运行

### Windows 软模拟环境（当前已验证）

#### 前置要求
- MinGW-w64 GCC（已通过MSYS2安装到 `C:\msys64\mingw64\bin`）
- CMake + Ninja（已随MSYS2安装）

#### 方法一：CMake + Ninja（推荐）

```powershell
# 1. 进入工程目录
cd d:\ai\prj\cb\pq_ai\pq_ai_terminal

# 2. 设置PATH（若未加入系统环境变量）
$env:PATH = "C:\msys64\mingw64\bin;C:\msys64\usr\bin;$env:PATH"

# 3. 配置
cmake -S . -B build -G "Ninja" -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Release

# 4. 编译
ninja -C build

# 5. 运行仿真
.\build\sim\pq_sim.exe --scenario S4 --cycles 100
```

#### 方法二：手动编译（无需CMake）

```batch
cd /d d:\ai\prj\cb\pq_ai\pq_ai_terminal
set PATH=C:\msys64\mingw64\bin;C:\msys64\usr\bin;%PATH%

:: 编译各模块
gcc -DPLATFORM_WINDOWS -Iinclude -Icore -Iai -Icomm -Iutils -Isim -Idrivers -O2 -std=c99 -c sim/sim_main.c -o sim_main.o
gcc -DPLATFORM_WINDOWS -Iinclude -Icore -Iai -Icomm -Iutils -Isim -Idrivers -O2 -std=c99 -c sim/hal_sim.c -o hal_sim.o
gcc -DPLATFORM_WINDOWS -Iinclude -Icore -Iai -Icomm -Iutils -Isim -Idrivers -O2 -std=c99 -c core/pq_metrics.c -o pq_metrics.o
gcc -DPLATFORM_WINDOWS -Iinclude -Icore -Iai -Icomm -Iutils -Isim -Idrivers -O2 -std=c99 -c core/event_trigger.c -o event_trigger.o
gcc -DPLATFORM_WINDOWS -Iinclude -Icore -Iai -Icomm -Iutils -Isim -Idrivers -O2 -std=c99 -c core/wave_freeze.c -o wave_freeze.o
gcc -DPLATFORM_WINDOWS -Iinclude -Icore -Iai -Icomm -Iutils -Isim -Idrivers -O2 -std=c99 -c core/feature_extract.c -o feature_extract.o
gcc -DPLATFORM_WINDOWS -Iinclude -Icore -Iai -Icomm -Iutils -Isim -Idrivers -O2 -std=c99 -c core/scenario_detect.c -o scenario_detect.o
gcc -DPLATFORM_WINDOWS -Iinclude -Icore -Iai -Icomm -Iutils -Isim -Idrivers -O2 -std=c99 -c ai/iforest_infer.c -o iforest_infer.o
gcc -DPLATFORM_WINDOWS -Iinclude -Icore -Iai -Icomm -Iutils -Isim -Idrivers -O2 -std=c99 -c ai/ae_infer.c -o ae_infer.o
gcc -DPLATFORM_WINDOWS -Iinclude -Icore -Iai -Icomm -Iutils -Isim -Idrivers -O2 -std=c99 -c ai/cnn1d_infer.c -o cnn1d_infer.o
gcc -DPLATFORM_WINDOWS -Iinclude -Icore -Iai -Icomm -Iutils -Isim -Idrivers -O2 -std=c99 -c comm/proto_mqtt.c -o proto_mqtt.o
gcc -DPLATFORM_WINDOWS -Iinclude -Icore -Iai -Icomm -Iutils -Isim -Idrivers -O2 -std=c99 -c comm/time_sync.c -o time_sync.o
gcc -DPLATFORM_WINDOWS -Iinclude -Icore -Iai -Icomm -Iutils -Isim -Idrivers -O2 -std=c99 -c utils/ring_buffer.c -o ring_buffer.o
gcc -DPLATFORM_WINDOWS -Iinclude -Icore -Iai -Icomm -Iutils -Isim -Idrivers -O2 -std=c99 -c utils/json_builder.c -o json_builder.o
gcc -DPLATFORM_WINDOWS -Iinclude -Icore -Iai -Icomm -Iutils -Isim -Idrivers -O2 -std=c99 -c utils/sqlite_wrapper.c -o sqlite_wrapper.o

:: 链接
gcc -o pq_sim.exe sim_main.o hal_sim.o pq_metrics.o event_trigger.o wave_freeze.o feature_extract.o scenario_detect.o iforest_infer.o ae_infer.o cnn1d_infer.o proto_mqtt.o time_sync.o ring_buffer.o json_builder.o sqlite_wrapper.o -lwinmm -lws2_32 -lm

:: 运行
pq_sim.exe --scenario S4 --cycles 100
```

### Linux 交叉编译（真实板卡部署）

```bash
cd pq_ai_terminal
mkdir -p build-linux
cd build-linux
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/aarch64-linux-gnu.cmake -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## 仿真程序用法

```
pq_sim.exe --scenario <S1|S2|S3|S4|S5> [--cycles N]
```

- `--scenario S1`：基准负荷（340kW工业负荷）
- `--scenario S2`：充电桩接入（80kW，5/7/11/13次谐波）
- `--scenario S3`：分布式光伏（200kW，电压抬升+2.93%）
- `--scenario S4`：光充耦合（280kW，谐波+电压抬升）
- `--scenario S5`：极端场景（360kW，高THD）
- `--cycles N`：运行N个周波（默认100）

## 核心模块说明

### 1. HAL仿真层（hal_sim.c）
- **功能**：模拟HT7627S的ADC采样、谐波计算、功率计量
- **波形生成**：基于场景参数的解析时域叠加法
  - 基波：`v_peak * sin(2*pi*50*t)`
  - 谐波：直接叠加各次谐波分量
  - 噪声：高斯白噪声，SNR≈55-60dB
- **寄存器计算**：从生成的波形中实时计算RMS、THD、功率等

### 2. PQ指标计算（pq_metrics.c）
- **电压偏差**：`(Vrms - Vnom) / Vnom * 100%`
- **THD**：直接从波形FFT分析提取
- **三相不平衡度**：基于RMS值的对称分量法简化实现
- **频率偏差**：`|f - 50| Hz`
- **变压器/线路负载率**：基于视在功率和额定容量

### 3. 事件触发引擎（event_trigger.c）
- **触发类型**：电压暂降/暂升、谐波超标、不平衡、过载、频率偏差
- **滞回机制**：进入阈值90%限值，退出阈值92%限值
- **严重度**：`severity = 10 * (实际值 / 限值)`

### 4. 场景识别（scenario_detect.c）
- **S2判定**：电流THD>8% 且 变压器负载率<40%
- **S3判定**：电压偏差>2% 且 电压THD 1.5-5% 且 电流THD<5%
- **S4判定**：同时满足电压抬升和谐波电流特征
- **S5判定**：电压THD>7% 或 电流THD>20% 或 负载率>90%

### 5. AI推理层（Stub）
- **孤立森林**：32棵随机树，8层深度，27维输入
- **自编码器**：27→8→27，tanh激活，CPU推理
- **1D-CNN**：8滤波器，5点卷积核，7类事件分类
- **当前状态**：使用随机权重模拟，待NPU工具链就绪后替换为INT8量化模型

## 输出与数据

运行后会在当前目录生成：
- `pq_metrics.csv`：每周期PQ指标记录
- `pq_events.csv`：触发事件记录
- 控制台输出：实时指标、事件告警、场景识别结果、治理建议

## 后续工作

### 待完成项
1. **真实HT7627S驱动**：替换 `hal_sim.c` 为SPI/I2C驱动，实现寄存器读写
2. **NPU模型部署**：将训练好的ONNX模型经INT8量化后，通过T536 NPU SDK部署
3. **E907 RTOS集成**：将核心采集任务迁移至E907核，A55负责AI推理
4. **rpmsg核间通信**：实现E907与A55之间的消息队列
5. **IEC 61850/MQTT完整协议栈**：替换stub实现，支持TLS加密
6. **SQLite本地存储**：替换CSV为嵌入式SQLite数据库

### 已知局限
- 功率因数计算受谐波影响，当前为近似值
- 场景识别规则为硬编码，后续应替换为AI分类模型
- AI模型为随机权重Stub，异常得分为绝对值，仅用于演示流程
- Windows仿真未模拟线路阻抗的动态电压降（直接叠加offset）

## 许可证

本项目为内部技术方案验证工程，仅供项目团队使用。

## 联系

项目：基于终端波形数据的电能质量AI应用
平台：T536 + HT7627S
日期：2026-08-02
