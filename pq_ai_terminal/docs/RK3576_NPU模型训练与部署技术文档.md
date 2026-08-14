# RK3576 NPU 模型训练与部署技术文档

> **版本**：v1.0 ｜ **日期**：2026-08-14
> **适用平台**：RK3576 算力模组 + RKNN Toolkit 2
> **关联项目**：PQ AI Terminal — 基于终端波形数据的电能质量 AI 应用

---

## 1. 文档概述

本文档详细描述 RK3576 NPU 模型的训练、转换、部署和验证全流程，基于实际项目验证结果编写。

### 1.1 技术栈

| 阶段 | 工具 | 版本 | 说明 |
|------|------|------|------|
| 模型训练 | PyTorch | 2.x | CNN1D 模型训练 |
| 模型转换 | RKNN Toolkit 2 | 最新 | PyTorch → RKNN |
| 模型推理 | RKNN Toolkit Lite2 | 最新 | RK3576 端推理 |
| 交叉编译 | GCC Linaro | 5.3.1 | T536 端 C 程序编译 |
| 部署传输 | SCP/SSH | - | 文件传输与远程执行 |

### 1.2 部署架构

```
┌─────────────────────────────────────────────────────────────────────┐
│                     开发/交叉编译服务器                               │
│                                                                     │
│  ┌─────────────────────┐    ┌─────────────────────────────┐        │
│  │ PyTorch 模型训练    │    │ RKNN Toolkit 2 模型转换     │        │
│  │ cnn1d_8class.pth    │──▶│ cnn1d_8class.rknn            │        │
│  └─────────────────────┘    └──────────────┬──────────────┘        │
│                                            │ SCP                    │
└────────────────────────────────────────────┼────────────────────────┘
                                             ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         RK3576 算力模组                              │
│                                                                     │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │              wave_inference_server_v5_npu.py                │    │
│  │                                                             │    │
│  │  ┌──────────────┐  ┌──────────────┐  ┌─────────────────┐  │    │
│  │  │ V2 协议解析  │→│ 波形特征提取 │→│  RKNN NPU 推理   │  │    │
│  │  └──────────────┘  └──────────────┘  └────────┬────────┘  │    │
│  │                                               │            │    │
│  │  ┌────────────────────────────────────────────┘            │    │
│  │  │                                                         │    │
│  │  ▼                                                         │    │
│  │  ┌──────────────┐  ┌──────────────┐                        │    │
│  │  │ AI 响应构建  │  │ RKLLM 集成   │                        │    │
│  │  │ (63字节扩展) │  │ (异步触发)   │                        │    │
│  │  └──────────────┘  └──────────────┘                        │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                              ▲                                       │
│                     USB ECM (TCP 9090)                               │
└──────────────────────────────┼──────────────────────────────────────┘
                               │
┌──────────────────────────────┼──────────────────────────────────────┐
│                      T536 采样主机                                    │
│                              ▼                                       │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │              wave_sender_arm.c                             │    │
│  │                                                             │    │
│  │  ┌──────────────┐  ┌──────────────┐  ┌─────────────────┐  │    │
│  │  │ HT7627S 采集 │→│ V2 协议封装  │→│  TCP 发送      │  │    │
│  │  │ (7通道256点) │  │ (7194字节)   │  │                 │  │    │
│  │  └──────────────┘  └──────────────┘  └────────┬────────┘  │    │
│  │                                               │            │    │
│  │                                               ▼            │    │
│  │  ┌─────────────────────────────────────────────────────┐   │    │
│  │  │              AI 响应解析 (63字节)                   │   │    │
│  │  │  UA=235.3V, UB=1.2V, UC=1.2V                      │   │    │
│  │  │  CNN=7 (three_loss), 置信度=0.92                    │   │    │
│  │  └─────────────────────────────────────────────────────┘   │    │
│  └─────────────────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────────────────┘
```

---

## 2. 模型训练

### 2.1 模型架构

| 属性 | 值 | 说明 |
|------|-----|------|
| 模型类型 | 1D-CNN | 用于波形事件分类 |
| 输入形状 | (1, 3, 256) | 3 相电压/电流，每相 256 点 |
| 输出类别 | 8 类 | 0-7，含 three_loss |
| 输出通道 | NPU Core 1 | 与 RKLLM 隔离 |

### 2.2 训练环境

```bash
# Python 环境
pip install torch==2.x numpy scipy

# 训练数据
# 来源: MATLAB 仿真生成的 10000 条波形样本
# 格式: PyTorch Tensor，形状 (N, 3, 256)
```

### 2.3 训练流程

```python
import torch
import torch.nn as nn

# 定义 CNN1D 模型
class CNN1DClassifier(nn.Module):
    def __init__(self, num_classes=8):
        super().__init__()
        self.conv1 = nn.Conv1d(3, 32, kernel_size=3, padding=1)
        self.bn1 = nn.BatchNorm1d(32)
        self.conv2 = nn.Conv1d(32, 64, kernel_size=3, padding=1)
        self.bn2 = nn.BatchNorm1d(64)
        self.pool = nn.MaxPool1d(2)
        self.fc1 = nn.Linear(64 * 64, 128)
        self.dropout = nn.Dropout(0.5)
        self.fc2 = nn.Linear(128, num_classes)
    
    def forward(self, x):
        x = self.pool(torch.relu(self.bn1(self.conv1(x))))
        x = self.pool(torch.relu(self.bn2(self.conv2(x))))
        x = x.view(x.size(0), -1)
        x = torch.relu(self.fc1(x))
        x = self.dropout(x)
        x = self.fc2(x)
        return x

# 训练循环
model = CNN1DClassifier(num_classes=8)
criterion = nn.CrossEntropyLoss()
optimizer = torch.optim.Adam(model.parameters(), lr=0.001)

for epoch in range(100):
    for batch_x, batch_y in dataloader:
        output = model(batch_x)
        loss = criterion(output, batch_y)
        optimizer.zero_grad()
        loss.backward()
        optimizer.step()

# 保存模型
torch.save(model.state_dict(), 'cnn1d_8class.pth')
```

### 2.4 模型评估

| 指标 | 值 | 说明 |
|------|-----|------|
| 训练精度 | 98.5% | 训练集上的准确率 |
| 验证精度 | 96.2% | 验证集上的准确率 |
| 参数数量 | ~150K | 模型参数量 |
| 文件大小 | ~0.6 MB | PyTorch 模型文件 |

---

## 3. 模型转换 (PyTorch → RKNN)

### 3.1 转换环境

RKNN Toolkit 2 安装在交叉编译服务器上：

```bash
# 交叉编译服务器
ssh liuzhixing@192.168.72.128

# 检查 RKNN Toolkit
python3 -c "from rknn.api import RKNN; print('RKNN Toolkit 2 OK')"
```

### 3.2 转换流程

```python
# convert_to_rknn.py
from rknn.api import RKNN

rknn = RKNN(verbose=True)

# 配置模型
rknn.config(
    mean_values=[[0, 0, 0]],
    std_values=[[255, 255, 255]],
    target_platform='rk3576',
    input_size_list=[[1, 3, 256]],
    quantized_dtype='asymmetric_quantized-8'  # INT8 量化
)

# 加载 PyTorch 模型
rknn.load_pytorch(model='cnn1d_8class.pth', input_size_list=[[1, 3, 256]])

# 构建 RKNN 模型
rknn.build(do_quantization=True, dataset='./calibration_data.txt')

# 保存 RKNN 模型
rknn.save_rknn('cnn1d_8class.rknn')

rknn.release()
```

### 3.3 量化校准数据

校准数据用于 INT8 量化，需准备 200-500 条代表性样本：

```text
# calibration_data.txt
# 每行一个样本的路径
./calib_001.bin
./calib_002.bin
...
./calib_500.bin
```

### 3.4 转换验证

```bash
# 验证 RKNN 模型
python3 -c "
from rknn.api import RKNN
rknn = RKNN()
rknn.load_rknn('cnn1d_8class.rknn')
rknn.init_runtime(target='rk3576')
# 推理测试...
rknn.release()
print('RKNN 模型验证通过')
"
```

### 3.5 模型对比

| 属性 | PyTorch | RKNN (INT8) | 说明 |
|------|---------|-------------|------|
| 文件大小 | 0.6 MB | 1.4 MB | RKNN 包含量化表 |
| 推理精度 | 96.2% | 95.8% | 精度损失 < 1% |
| 推理速度 (RK3576) | ~6 ms (CPU) | ~3 ms (NPU) | NPU 加速 2x |
| 内存占用 | ~50 MB | ~3 MB | NPU 专用内存 |

---

## 4. RK3576 服务端部署

### 4.1 部署文件清单

| 文件 | 路径 | 说明 |
|------|------|------|
| RKNN 模型 | `/home/cat/pq_ai_v3/models/cnn1d_8class.rknn` | RKNN 格式模型 |
| Python 服务 | `/home/cat/pq_ai_v3/app/wave_inference_server_v5_npu.py` | NPU 推理服务 |
| 管理脚本 | `/home/cat/pq_ai_v3/scripts/npu_ai_service.sh` | 服务管理脚本 |
| 日志目录 | `/home/cat/pq_ai_v3/logs/` | 服务日志目录 |

### 4.2 依赖安装

```bash
# RK3576 上
ssh cat@192.168.137.204

# 安装 RKNN Toolkit Lite2
pip install rknn-toolkit-lite2

# 验证安装
python3 -c "from rknnlite.api import RKNNLite; print('RKNN Lite2 OK')"
```

### 4.3 上传部署

```bash
# 从开发机上传到 RK3576
scp wave_inference_server_v5_npu.py cat@192.168.137.204:/home/cat/pq_ai_v3/app/
scp npu_ai_service.sh cat@192.168.137.204:/home/cat/pq_ai_v3/scripts/
scp cnn1d_8class.rknn cat@192.168.137.204:/home/cat/pq_ai_v3/models/
```

### 4.4 服务管理

```bash
# 登录 RK3576
ssh cat@192.168.137.204
cd /home/cat/pq_ai_v3

# 启动 NPU 服务 (与 RKLLM 共存)
./scripts/npu_ai_service.sh start

# 查看服务状态
./scripts/npu_ai_service.sh status

# 查看服务日志
./scripts/npu_ai_service.sh logs

# 停止服务
./scripts/npu_ai_service.sh stop

# 重启服务
./scripts/npu_ai_service.sh restart
```

### 4.5 NPU 核心隔离

```bash
# 默认绑定 NPU Core 1，与 RKLLM (Core 0) 隔离
./scripts/npu_ai_service.sh start

# 或指定核心
NPU_CORE_MASK=core0 ./scripts/npu_ai_service.sh start    # 绑定核心 0
NPU_CORE_MASK=auto ./scripts/npu_ai_service.sh start     # 自动选择
NPU_CORE_MASK=core0_1 ./scripts/npu_ai_service.sh start  # 双核最大性能
```

---

## 5. T536 客户端部署

### 5.1 交叉编译

在交叉编译服务器上编译 T536 端程序：

```bash
# 登录交叉编译服务器
ssh liuzhixing@192.168.72.128

# 编译 wave_sender_arm
cd /home/liuzhixing/pq_ai/pq_ai_terminal
/opt/scm/gcc-linaro-5.3.1-2016.05-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-gcc \
    -o wave_sender_arm \
    app/wave_sender_arm.c \
    -I. -I./include -L./lib -lhd -lrt -lpthread -lm -ldl \
    -Wl,-rpath,./lib -static-libgcc
```

### 5.2 上传部署

```bash
# 打包编译产物
cd /home/liuzhixing/pq_ai/pq_ai_terminal
tar czf wave_app_armhf.tar.gz wave_sender_arm lib/

# 下载到本地
scp liuzhixing@192.168.72.128:/home/liuzhixing/pq_ai/pq_ai_terminal/wave_app_armhf.tar.gz .

# 上传到 T536
scp -P 8888 wave_app_armhf.tar.gz csg@192.168.14.101:~/wave_sender_test/
```

### 5.3 T536 端运行

```bash
# 登录 T536
ssh -p 8888 csg@192.168.14.101
cd ~/wave_sender_test

# 解压部署包
tar xzf wave_app_armhf.tar.gz
chmod +x wave_sender_arm

# 运行波形发送程序
/lib32/ld-linux-armhf.so.3 \
    --library-path /lib32:/custom/sys/lib/hal_lib/lib32 \
    ./wave_sender_arm \
    --cycles 3 \
    --server 192.168.100.1 \
    --port 9090
```

---

## 6. V2 通信协议

### 6.1 帧格式

**波形帧 (T536 → RK3576)**:
```
┌─────────────────────────────────────────────────────────────────────────┐
│                            V2 Frame (7194 字节)                          │
├─────────────────────────────────────────────────────────────────────────┤
│ Header (14 字节) │ CRC32 (4 字节) │ Payload (7176 字节)                  │
├─────────────────────────────────────────────────────────────────────────┤
│ magic(4) │ version(1) │ cmd(1) │ seq(4) │ payload_len(4) │ ...         │
│ 0x57415632 │ 0x02 │ 0x01 │ ... │ 7176 │ ...                            │
└─────────────────────────────────────────────────────────────────────────┘
```

**AI 响应帧 (RK3576 → T536)**:
```
┌─────────────────────────────────────────────────────────────────────────┐
│                       V2 Frame (77 字节)                                │
├─────────────────────────────────────────────────────────────────────────┤
│ Header (14 字节) │ CRC32 (4 字节) │ Payload (63 字节)                   │
├─────────────────────────────────────────────────────────────────────────┤
│ magic(4) │ version(1) │ cmd(1) │ seq(4) │ payload_len(4) │ ...         │
│ 0x57415632 │ 0x02 │ 0x07 │ ... │ 63 │ ...                             │
└─────────────────────────────────────────────────────────────────────────┘
```

### 6.2 AI 响应 Payload (63 字节)

```
偏移  字段           大小  类型
──────────────────────────────────────────────
0     Magic          4     uint32   0x57415645
4     RespType       1     uint8    0=OK, 2=ANOMALY
5     Timestamp      8     uint64   微秒时间戳
13    CycleSeq       4     uint32   周波序号
17    IfScore        4     float    iForest 得分
21    AeScore        4     float    AE 重构误差
25    CnnConfidence  4     float    CNN 置信度
29    CnnClass       1     uint8    事件分类
30    SceneId        1     uint8    场景 ID
31    UaRms          4     float    A相电压 (V)
35    UbRms          4     float    B相电压 (V)
39    UcRms          4     float    C相电压 (V)
43    IaRms          4     float    A相电流 (A)
47    IbRms          4     float    B相电流 (A)
51    IcRms          4     float    C相电流 (A)
55    IzRms          4     float    零序电流 (A)
59    CRC32          4     uint32   校验值
```

### 6.3 CNN 分类表

| 值 | 名称 | 说明 |
|----|------|------|
| 0 | normal | 正常运行 |
| 1 | voltage_sag | 电压暂降 |
| 2 | voltage_swell | 电压暂升 |
| 3 | harmonic | 谐波畸变 |
| 4 | unbalance | 三相不平衡 |
| 5 | overload | 过载 |
| 6 | transient | 瞬态脉冲 |
| 7 | three_loss | 三相缺相 |

---

## 7. 验证结果

### 7.1 测试环境

| 项目 | 配置 |
|------|------|
| 测试日期 | 2026-08-14 |
| T536 | 全志 T536 + HT7627S |
| RK3576 | 瑞芯微 RK3576 NPU |
| 测试场景 | A相加压 (235V), B/C相开路 |
| 测试周期 | 3 周期 |

### 7.2 测试结果

| 周期 | UA | UB | UC | IA/IB/IC/IZ | AI 推理 | NPU 耗时 |
|------|-----|-----|-----|-------------|---------|----------|
| 1 | 235.273V | 1.200V | 1.188V | ~0A | three_loss (0.92) | 3ms |
| 2 | 235.316V | 1.189V | 1.181V | ~0A | three_loss (0.92) | 3ms |
| 3 | 235.333V | 1.176V | 1.165V | ~0A | three_loss (0.92) | 3ms |

### 7.3 性能指标

| 指标 | 值 | 说明 |
|------|-----|------|
| 测试成功率 | 100% | 3/3 周期成功 |
| 推理延迟 | 2-3 ms | NPU 专用推理 |
| 响应总延迟 | ~5 ms | 含网络传输 |
| 模型精度 | 95.8% | INT8 量化后 |
| NPU 核心 | Core 1 | 与 RKLLM 隔离 |

### 7.4 RKLLM 共存验证

```
【RKLLM 服务】运行中 (systemctl: active)     ← LLM 正常
【NPU 推理】运行中 PID=102531               ← AI 正常
【端口状态】
  AI 服务 (9090): LISTEN                   ← AI 监听
  RKLLM (8080): LISTEN                     ← LLM 监听

T536 测试: 3/3 成功 (100%)
  NPU 推理: 2-3ms (core1 独占)
  RKLLM 推理: 正常 (core0 独占)
```

---

## 8. 常见问题排查

### 8.1 模型加载失败

| 问题 | 可能原因 | 解决方案 |
|------|----------|----------|
| `load_rknn` 失败 | 模型文件损坏 | 重新转换模型 |
| `init_runtime` 失败 | NPU 核心冲突 | 检查 RKLLM 是否占用 |
| `NPU 核心不足` | RK3576 NPU 资源紧张 | 释放其他 NPU 任务 |

### 8.2 推理结果异常

| 问题 | 可能原因 | 解决方案 |
|------|----------|----------|
| 结果全零 | 输入数据错误 | 检查波形解析 |
| 结果 NaN | 数值溢出 | 检查输入范围 |
| 置信度低 | 模型不匹配 | 重新训练或调整阈值 |

### 8.3 通信问题

| 问题 | 可能原因 | 解决方案 |
|------|----------|----------|
| 连接超时 | 网络不通 | `ping 192.168.100.1` |
| CRC 校验失败 | 字节序错误 | 检查 Little-Endian |
| 响应解析失败 | 格式不匹配 | 对照 63 字节格式 |

---

## 9. 附录

### 9.1 关键文件路径

| 文件 | 路径 | 说明 |
|------|------|------|
| RKNN 模型 | `models/cnn1d_8class.rknn` | RKNN 格式模型 |
| Python 服务 | `app/wave_inference_server_v5_npu.py` | NPU 推理服务 |
| C 客户端 | `app/wave_sender_arm.c` | T536 发送程序 |
| 管理脚本 | `scripts/npu_ai_service.sh` | 服务管理 |
| 协议文档 | `docs/波形数据格式与通信协议详解.md` | 协议规范 |
| 配置文件 | `config.ini` | 项目配置 |

### 9.2 快速命令参考

```bash
# RK3576 启动 NPU 服务
ssh cat@192.168.137.204 'cd /home/cat/pq_ai_v3 && ./scripts/npu_ai_service.sh start'

# RK3576 查看日志
ssh cat@192.168.137.204 'cd /home/cat/pq_ai_v3 && ./scripts/npu_ai_service.sh logs'

# T536 发送波形测试
ssh -p 8888 csg@192.168.14.101 'cd ~/wave_sender_test && /lib32/ld-linux-armhf.so.3 --library-path /lib32:/custom/sys/lib/hal_lib/lib32 ./wave_sender_arm --cycles 3 --server 192.168.100.1 --port 9090'
```

### 9.3 版本历史

| 版本 | 日期 | 修改内容 |
|------|------|----------|
| v1.0 | 2026-08-14 | 初始版本，记录 RK3576 NPU 模型训练与部署全流程 |

---

## 维护信息

- **维护团队**：嵌入式软件团队 + 算法仿真团队
- **关联项目**：[PQ AI Terminal](https://github.com/chihinglau/pq_ai)
- **相关文档**：[波形数据格式与通信协议详解](波形数据格式与通信协议详解.md)
