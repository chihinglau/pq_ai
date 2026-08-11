# RK3576 RKLLM 推理服务部署与调用技术方案

## 1. 系统架构概述

### 1.1 整体架构

```
┌─────────────────────────────────────────────────────────────────┐
│                        客户端层                                  │
│  ┌─────────────────────┐  ┌─────────────────────────────────┐  │
│  │  PC 客户端           │  │  ARM 客户端 (其他板子)            │  │
│  │  (Python/Node.js)    │  │  (C/MQTT客户端)                  │  │
│  └──────────┬──────────┘  └──────────────┬────────────────────┘  │
│             │ HTTP/HTTPS                │ MQTT                    │
└─────────────┼────────────────────────────┼───────────────────────┘
              │                            │
              ▼                            ▼
┌─────────────────────────────────────────────────────────────────┐
│                     RK3576 开发板 (192.168.137.204)              │
│                                                                 │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │                 Flask Web Server (Port: 8080)             │  │
│  │  ┌─────────┐ ┌─────────┐ ┌───────────┐ ┌───────────┐   │  │
│  │  │ /health │ │/v1/model│ │/v1/chat/  │ │ /generate │   │  │
│  │  │         │ │  s      │ │completions│ │           │   │  │
│  │  └────┬────┘ └────┬────┘ └─────┬─────┘ └─────┬─────┘   │  │
│  │       └────────────┴────────────┴─────────────┘         │  │
│  │                            │                            │  │
│  │                    线程锁 (Thread Lock)                  │  │
│  └──────────────────────────┬───────────────────────────────┘  │
│                             │ 子进程调用                        │
│  ┌──────────────────────────▼───────────────────────────────┐  │
│  │                  llama-cli (RKLLM后端)                    │  │
│  │  ┌─────────────────────────────────────────────────────┐ │  │
│  │  │  RKLLM Runtime (librkllmrt.so)                     │ │  │
│  │  │  - NPU 加速推理                                      │ │  │
│  │  │  - CPU 协同处理                                      │ │  │
│  │  └─────────────────────────────────────────────────────┘ │  │
│  └───────────────────────────────────────────────────────────┘  │
│                                                                 │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │              MQTT Bridge Service (Port: 1883)             │  │
│  │  ┌─────────────────────────────────────────────────────┐ │  │
│  │  │  rkllm_mqtt_bridge.py                              │ │  │
│  │  │  - 监听 rkllm/request/* 主题                        │ │  │
│  │  │  - 转发请求到 Flask API                             │ │  │
│  │  │  - 响应发布到 rkllm/response/<request_id>          │ │  │
│  │  └─────────────────────────────────────────────────────┘ │  │
│  └───────────────────────────────────────────────────────────┘  │
│                                                                 │
│  硬件资源:                                                      │
│  ├── NPU: 950MHz (4核)                                         │
│  ├── CPU: 1.6GHz (8核)                                         │
│  ├── GPU: 300MHz                                               │
│  └── DDR: 528MHz                                               │
└─────────────────────────────────────────────────────────────────┘
```

### 1.2 技术栈

| 组件 | 版本/规格 | 说明 |
|------|-----------|------|
| 处理器 | RK3576 | 4×Cortex-A76 + 4×Cortex-A55 |
| NPU | 6 TOPS | 4核独立NPU |
| 系统 | Ubuntu 22.04 LTS | 64-bit ARM64 |
| Python | 3.10.12 | 系统默认Python |
| Flask | 3.x | Web框架 |
| llama-cli | 基于llama.cpp | RKLLM后端推理引擎 |
| RKLLM Runtime | 1.3.0 | 模型推理运行时 |
| Mosquitto | 1.6 | MQTT Broker |
| libmosquitto | 1.6 | MQTT客户端库(C) |

---

## 2. 硬件与环境要求

### 2.1 最小硬件配置

```
CPU:    8核 (4×A76 + 4×A55)
NPU:    4核 NPU (6 TOPS)
内存:   ≥ 8GB DDR
存储:   ≥ 32GB eMMC/SD
网络:   以太网 (100Mbps+) 或 WiFi
```

### 2.2 软件依赖

```bash
# Python 依赖
python3 >= 3.10
flask
flask-cors
paho-mqtt

# 系统依赖
libgl1-mesa-glx
libgomp1
libnuma1
mosquitto
libmosquitto-dev
```

### 2.3 网络配置

```
板子默认IP: 192.168.137.204 (可根据实际网络调整)
HTTP服务端口: 8080
MQTT服务端口: 1883
协议:       HTTP / MQTT
```

---

## 3. 部署方案

### 3.1 一键部署（推荐）

#### 3.1.1 获取部署包

部署包 `rkllm_deploy_v1.0.0.run` 包含所有必要文件，支持一键安装。

#### 3.1.2 执行部署

```bash
# 1. 拷贝部署包到RK3576板子
scp rkllm_deploy_v1.0.0.run cat@<RK3576_IP>:/home/cat/

# 2. 赋予执行权限
chmod +x rkllm_deploy_v1.0.0.run

# 3. 交互式安装
./rkllm_deploy_v1.0.0.run

# 4. 静默安装（跳过提示）
./rkllm_deploy_v1.0.0.run -y

# 5. 自定义安装目录
./rkllm_deploy_v1.0.0.run -d /custom/path

# 6. 查看帮助
./rkllm_deploy_v1.0.0.run -h
```

#### 3.1.3 部署流程

run包执行时将自动完成以下步骤：

1. **检查环境**：验证操作系统、Python版本、依赖库
2. **安装依赖**：自动安装Flask、Mosquitto等必要组件
3. **复制文件**：将服务程序、脚本、配置文件安装到目标目录
4. **配置服务**：设置systemd服务，配置开机自启
5. **启动服务**：启动Mosquitto Broker、RKLLM Server、MQTT Bridge

### 3.2 目录结构

部署完成后的目标目录结构如下：

```
/home/cat/
├── rkllm-server-deploy/           # 主部署目录
│   ├── src/                       # 服务程序目录
│   │   └── rkllm_server_v4.py     # Flask服务器主程序
│   ├── scripts/                   # 管理脚本目录
│   │   ├── rkllm_service.sh       # 服务管理脚本
│   │   ├── rkllm_client.py        # Python客户端
│   │   ├── rkllm_client_fixed.sh  # Shell客户端
│   │   ├── rkllm_mqtt_bridge.py   # MQTT桥接服务
│   │   ├── test_api.sh            # API性能测试
│   │   └── test_api_full.sh       # 全功能测试
│   ├── configs/                   # 配置文件目录
│   │   ├── rkllm-server.service   # Flask服务配置
│   │   └── rkllm-mqtt-bridge.service # MQTT桥接服务配置
│   ├── client/                    # C客户端目录
│   │   ├── rkllm_client           # ARM架构客户端可执行文件
│   │   ├── lib/                   # 依赖库
│   │   │   ├── libmosquitto.so.1
│   │   │   ├── libssl.so.1.0.0
│   │   │   └── libcrypto.so.1.0.0
│   │   ├── rkllm_mqtt_client.c    # 客户端源码
│   │   ├── main.c                 # 主程序
│   │   ├── Makefile               # 编译配置
│   │   └── cross_compile.sh       # 交叉编译脚本
│   └── logs/                      # 日志目录
│       ├── service.log            # 标准输出日志
│       └── service_error.log      # 错误输出日志
│
├── ai/
│   ├── models/                    # 模型文件目录
│   │   └── qwen3-1.7b-rk3576.rkllm  # RKLLM格式模型
│   └── rk-llama.cpp-rknpu2/       # llama-cli编译产物
│       └── build_rk3576/
│           └── bin/
│               └── llama-cli      # 推理可执行文件
│
└── ...
```

### 3.3 手动部署脚本

如需要手动部署，可使用以下脚本：

```bash
#!/bin/bash
# RKLLM 推理服务一键部署脚本
# 使用方法: chmod +x deploy_rkllm.sh && sudo ./deploy_rkllm.sh

set -e

echo "=============================================="
echo "  RKLLM 推理服务部署脚本"
echo "=============================================="

# 配置变量
DEPLOY_DIR="/home/cat/rkllm-server-deploy"
SRC_DIR="${DEPLOY_DIR}/src"
SCRIPTS_DIR="${DEPLOY_DIR}/scripts"
CONFIGS_DIR="${DEPLOY_DIR}/configs"
CLIENT_DIR="${DEPLOY_DIR}/client"
LOGS_DIR="${DEPLOY_DIR}/logs"
MODEL_FILE="/home/cat/ai/models/qwen3-1.7b-rk3576.rkllm"
LLAMA_CLI="/home/cat/ai/rk-llama.cpp-rknpu2/build_rk3576/bin/llama-cli"

echo "[1/7] 创建目录结构..."
mkdir -p "${SRC_DIR}"
mkdir -p "${SCRIPTS_DIR}"
mkdir -p "${CONFIGS_DIR}"
mkdir -p "${CLIENT_DIR}/lib"
mkdir -p "${LOGS_DIR}"

echo "[2/7] 检查并安装Python依赖..."
pip3 install flask flask-cors paho-mqtt 2>/dev/null || \
pip3 install flask flask-cors paho-mqtt --break-system-packages

echo "[3/7] 检查并安装Mosquitto..."
if ! command -v mosquitto &> /dev/null; then
    sudo apt-get update -qq
    sudo apt-get install -y mosquitto mosquitto-clients libmosquitto-dev
fi
sudo systemctl enable mosquitto
sudo systemctl start mosquitto

echo "[4/7] 检查llama-cli和模型文件..."
if [ ! -f "${LLAMA_CLI}" ]; then
    echo "错误: llama-cli 不存在于 ${LLAMA_CLI}"
    echo "请先编译 rk-llama.cpp-rknpu2"
    exit 1
fi
chmod +x "${LLAMA_CLI}"

if [ ! -f "${MODEL_FILE}" ]; then
    echo "错误: 模型文件不存在于 ${MODEL_FILE}"
    exit 1
fi

echo "[5/7] 部署systemd服务..."

# Flask服务配置
cat > /etc/systemd/system/rkllm-server.service << 'SERVICE_EOF'
[Unit]
Description=RKLLM Flask Server
After=network.target mosquitto.service

[Service]
Type=simple
User=cat
Group=cat
WorkingDirectory=/home/cat/rkllm-server-deploy/scripts

Environment=LD_LIBRARY_PATH=/usr/lib:/usr/local/lib
Environment=PYTHONUNBUFFERED=1

ExecStart=/usr/bin/python3 /home/cat/rkllm-server-deploy/src/rkllm_server_v4.py \
    --model /home/cat/ai/models/qwen3-1.7b-rk3576.rkllm \
    --llama-bin /home/cat/ai/rk-llama.cpp-rknpu2/build_rk3576/bin/llama-cli \
    --port 8080 \
    --threads 4 \
    --max-tokens 256

Restart=always
RestartSec=5
MemoryMax=2G
CPUQuota=80%
StandardOutput=append:/home/cat/rkllm-server-deploy/logs/service.log
StandardError=append:/home/cat/rkllm-server-deploy/logs/service_error.log

[Install]
WantedBy=multi-user.target
SERVICE_EOF

# MQTT桥接服务配置
cat > /etc/systemd/system/rkllm-mqtt-bridge.service << 'BRIDGE_EOF'
[Unit]
Description=RKLLM MQTT Bridge Service
After=mosquitto.service rkllm-server.service
Requires=mosquitto.service

[Service]
Type=simple
User=cat
Group=cat
WorkingDirectory=/home/cat/rkllm-server-deploy/scripts

ExecStart=/usr/bin/python3 /home/cat/rkllm-server-deploy/scripts/rkllm_mqtt_bridge.py

Restart=always
RestartSec=3
StandardOutput=append:/home/cat/rkllm-server-deploy/logs/bridge.log
StandardError=append:/home/cat/rkllm-server-deploy/logs/bridge_error.log

[Install]
WantedBy=multi-user.target
BRIDGE_EOF

sudo systemctl daemon-reload
sudo systemctl enable rkllm-server
sudo systemctl enable rkllm-mqtt-bridge

echo "[6/7] 设置文件权限..."
chmod +x "${SCRIPTS_DIR}/rkllm_service.sh" 2>/dev/null || true
chmod +x "${SCRIPTS_DIR}/rkllm_client.py" 2>/dev/null || true
chmod +x "${SCRIPTS_DIR}/rkllm_client_fixed.sh" 2>/dev/null || true
chown -R cat:cat "${DEPLOY_DIR}"

echo "[7/7] 启动服务..."
sudo systemctl start rkllm-server
sleep 3
sudo systemctl start rkllm-mqtt-bridge
sleep 1

# 检查服务状态
echo ""
echo "=============================================="
echo "  部署完成!"
echo "=============================================="
echo ""
echo "  HTTP服务: http://$(hostname -I | awk '{print $1}'):8080"
echo "  MQTT服务: $(hostname -I | awk '{print $1}'):1883"
echo ""
echo "  API 接口:"
echo "    GET  /health              健康检查"
echo "    GET  /v1/models           模型列表"
echo "    POST /v1/chat/completions 聊天补全"
echo "    POST /generate            简单生成"
echo ""
echo "  MQTT 主题:"
echo "    rkllm/request/health      健康检查"
echo "    rkllm/request/models      模型列表"
echo "    rkllm/request/chat        聊天请求"
echo "    rkllm/request/generate    生成请求"
echo ""

# 验证服务
if curl -s http://localhost:8080/health > /dev/null 2>&1; then
    echo "  状态: 服务运行正常"
else
    echo "  状态: 服务启动中，请等待..."
    echo "  查看日志: journalctl -u rkllm-server -f"
fi
```

---

## 4. 服务管理

### 4.1 一键管理命令

```bash
# 查看所有服务状态
sudo systemctl status rkllm-server
sudo systemctl status rkllm-mqtt-bridge
sudo systemctl status mosquitto

# 一键启动所有服务
sudo systemctl start mosquitto
sudo systemctl start rkllm-server
sudo systemctl start rkllm-mqtt-bridge

# 一键停止所有服务
sudo systemctl stop rkllm-mqtt-bridge
sudo systemctl stop rkllm-server
sudo systemctl stop mosquitto

# 一键重启所有服务
sudo systemctl restart rkllm-server
sudo systemctl restart rkllm-mqtt-bridge
```

### 4.2 脚本管理

```bash
cd /home/cat/rkllm-server-deploy/scripts

# 使用服务管理脚本
./rkllm_service.sh start           # 启动
./rkllm_service.sh stop            # 停止
./rkllm_service.sh restart          # 重启
./rkllm_service.sh status           # 查看状态
./rkllm_service.sh logs             # 查看日志
./rkllm_service.sh logs 100        # 查看最近100行日志
./rkllm_service.sh test            # 运行测试
```

### 4.3 服务依赖关系

```
mosquitto (MQTT Broker)
    ├── rkllm-server (Flask API)
    │   └── rkllm-mqtt-bridge (MQTT桥接)
    └── rkllm-mqtt-bridge (MQTT桥接)
```

> **注意**: 启动顺序为 mosquitto → rkllm-server → rkllm-mqtt-bridge

---

## 5. API 接口参考

### 5.1 HTTP API（推荐）

#### 5.1.1 健康检查

**GET /health**

```bash
curl http://192.168.137.204:8080/health
```

响应：
```json
{
    "status": "ready",
    "model": "qwen3-1.7b-rk3576.rkllm",
    "platform": "RK3576",
    "uptime": "120s",
    "timestamp": "2026-08-10T12:00:00.000000"
}
```

#### 5.1.2 模型列表

**GET /v1/models**

```bash
curl http://192.168.137.204:8080/v1/models
```

响应：
```json
{
    "object": "list",
    "data": [{
        "id": "qwen3-1.7b-rk3576.rkllm",
        "object": "model",
        "created": 1786000000,
        "owned_by": "rkllm-server"
    }]
}
```

#### 5.1.3 聊天补全（OpenAI兼容）

**POST /v1/chat/completions**

请求：
```bash
curl -X POST http://192.168.137.204:8080/v1/chat/completions \
    -H "Content-Type: application/json" \
    -d '{
        "messages": [
            {"role": "system", "content": "你是一个有用的AI助手"},
            {"role": "user", "content": "你好，请介绍一下自己"}
        ],
        "max_tokens": 50
    }'
```

响应：
```json
{
    "id": "chatcmpl-1786000000",
    "object": "chat.completion",
    "created": 1786000000,
    "model": "qwen3-1.7b-rk3576.rkllm",
    "choices": [{
        "index": 0,
        "message": {
            "role": "assistant",
            "content": "我是一个AI助手，可以帮助你回答问题..."
        },
        "finish_reason": "stop"
    }],
    "usage": {
        "prompt_tokens": 25,
        "completion_tokens": 30,
        "total_tokens": 55
    },
    "elapsed_seconds": 7.82
}
```

#### 5.1.4 流式输出

**POST /v1/chat/completions (streaming)**

请求：
```bash
curl -X POST http://192.168.137.204:8080/v1/chat/completions \
    -H "Content-Type: application/json" \
    -d '{
        "messages": [{"role": "user", "content": "写一首关于春天的诗"}],
        "max_tokens": 100,
        "stream": true
    }'
```

流式响应（SSE格式）：
```
data: {"id":"chatcmpl-xxx","object":"chat.completion.chunk","choices":[{"delta":{"content":"春"},"finish_reason":null}]}

data: {"id":"chatcmpl-xxx","object":"chat.completion.chunk","choices":[{"delta":{"content":"天"},"finish_reason":null}]}

...

data: {"id":"chatcmpl-xxx","object":"chat.completion.chunk","choices":[{"delta":{},"finish_reason":"stop"}]}

data: [DONE]
```

#### 5.1.5 简单生成

**POST /generate**

请求：
```bash
curl -X POST http://192.168.137.204:8080/generate \
    -H "Content-Type: application/json" \
    -d '{
        "prompt": "请解释什么是人工智能",
        "max_tokens": 80
    }'
```

响应：
```json
{
    "generated_text": "人工智能是计算机科学的一个分支...",
    "usage": {
        "prompt_tokens": 15,
        "completion_tokens": 80,
        "total_tokens": 95
    },
    "elapsed_seconds": 8.45
}
```

### 5.2 MQTT API（适用于ARM客户端）

#### 5.2.1 MQTT主题结构

```
请求主题: rkllm/request/<action>
响应主题: rkllm/response/<request_id>
```

#### 5.2.2 健康检查

```bash
# 发送请求
mosquitto_pub -h 192.168.137.204 -t "rkllm/request/health" \
    -m '{"request_id": "test001"}'

# 订阅响应
mosquitto_sub -h 192.168.137.204 -t "rkllm/response/test001" -C 1
```

响应：
```json
{
    "request_id": "test001",
    "action": "health",
    "data": {
        "status": "ready",
        "model": "qwen3-1.7b-rk3576.rkllm",
        "platform": "RK3576"
    }
}
```

#### 5.2.3 聊天请求

```bash
# 发送请求
mosquitto_pub -h 192.168.137.204 -t "rkllm/request/chat" \
    -m '{
        "request_id": "chat001",
        "messages": [
            {"role": "user", "content": "你好"}
        ],
        "max_tokens": 50
    }'

# 订阅响应
mosquitto_sub -h 192.168.137.204 -t "rkllm/response/chat001" -C 1
```

响应：
```json
{
    "request_id": "chat001",
    "action": "chat",
    "data": {
        "choices": [{
            "message": {
                "role": "assistant",
                "content": "你好！有什么可以帮助你的吗？"
            }
        }],
        "elapsed_seconds": 5.23
    }
}
```

#### 5.2.4 支持的Action

| Action | 主题 | 说明 |
|--------|------|------|
| health | rkllm/request/health | 健康检查 |
| models | rkllm/request/models | 获取模型列表 |
| chat | rkllm/request/chat | 聊天补全 |
| generate | rkllm/request/generate | 文本生成 |

---

## 6. 客户端使用

### 6.1 Python 客户端

#### 6.1.1 HTTP客户端

```python
import requests

# 配置
SERVER_URL = "http://192.168.137.204:8080"

def chat(messages, max_tokens=100):
    """发送聊天请求"""
    response = requests.post(
        f"{SERVER_URL}/v1/chat/completions",
        json={
            "messages": messages,
            "max_tokens": max_tokens
        },
        timeout=120
    )
    response.raise_for_status()
    return response.json()["choices"][0]["message"]["content"]

def chat_stream(messages, max_tokens=100):
    """流式聊天请求"""
    response = requests.post(
        f"{SERVER_URL}/v1/chat/completions",
        json={
            "messages": messages,
            "max_tokens": max_tokens,
            "stream": True
        },
        stream=True,
        timeout=120
    )
    
    full_response = ""
    for line in response.iter_lines():
        if line:
            data = line.decode()
            if data.startswith("data: "):
                json_str = data[6:]
                if json_str == "[DONE]":
                    break
                chunk = json.loads(json_str)
                delta = chunk["choices"][0].get("delta", {})
                if "content" in delta:
                    full_response += delta["content"]
    return full_response

# 使用示例
if __name__ == "__main__":
    # 单轮对话
    messages = [
        {"role": "user", "content": "你好，请介绍一下自己"}
    ]
    print("回答:", chat(messages))
    
    # 多轮对话
    messages = [
        {"role": "system", "content": "你是一个AI助手"},
        {"role": "user", "content": "我叫小明"},
        {"role": "assistant", "content": "你好小明！很高兴认识你。"},
        {"role": "user", "content": "我叫什么名字？"}
    ]
    print("回答:", chat(messages))
    
    # 流式输出
    print("\n流式输出:")
    for chunk in chat_stream([{"role": "user", "content": "写一首关于秋天的诗"}]):
        print(chunk, end="", flush=True)
```

#### 6.1.2 MQTT客户端

```python
import paho.mqtt.client as mqtt
import json
import time

# 配置
BROKER_HOST = "192.168.137.204"
BROKER_PORT = 1883

class RKLLMMQTTClient:
    def __init__(self, host, port=1883):
        self.host = host
        self.port = port
        self.client = mqtt.Client()
        self.client.on_message = self.on_message
        self.client.connect(host, port)
        self.client.loop_start()
        self.response_cache = {}
    
    def on_message(self, client, userdata, msg):
        topic = msg.topic
        payload = json.loads(msg.payload.decode())
        request_id = topic.split("/")[-1]
        self.response_cache[request_id] = payload
    
    def request(self, action, data=None, timeout=120):
        request_id = str(int(time.time() * 1000))
        payload = {"request_id": request_id, **(data or {})}
        
        self.client.publish(f"rkllm/request/{action}", json.dumps(payload))
        
        # 等待响应
        self.client.subscribe(f"rkllm/response/{request_id}")
        start_time = time.time()
        while request_id not in self.response_cache:
            if time.time() - start_time > timeout:
                self.client.unsubscribe(f"rkllm/response/{request_id}")
                return None
            time.sleep(0.1)
        
        self.client.unsubscribe(f"rkllm/response/{request_id}")
        return self.response_cache.pop(request_id)
    
    def health_check(self):
        return self.request("health")
    
    def chat(self, messages, max_tokens=100):
        return self.request("chat", {
            "messages": messages,
            "max_tokens": max_tokens
        })

# 使用示例
if __name__ == "__main__":
    client = RKLLMMQTTClient(BROKER_HOST)
    
    # 健康检查
    health = client.health_check()
    print("健康检查:", health)
    
    # 聊天请求
    response = client.chat([
        {"role": "user", "content": "你好"}
    ])
    print("AI回复:", response)
```

### 6.2 Shell 客户端

```bash
# 使用rkllm_client_fixed.sh
bash /home/cat/rkllm-server-deploy/scripts/rkllm_client_fixed.sh 192.168.137.204

# 使用mosquitto命令行
# 健康检查
mosquitto_pub -h 192.168.137.204 -t "rkllm/request/health" -m '{"request_id":"test"}'
mosquitto_sub -h 192.168.137.204 -t "rkllm/response/test" -C 1
```

### 6.3 C/MQTT 客户端（ARM架构）

#### 6.3.1 客户端功能

C/MQTT客户端是为其他ARM板子设计的轻量级客户端，支持通过MQTT协议与RK3576板子通信。

功能特性：
- 支持MQTT协议连接
- 支持健康检查、模型列表、聊天、生成等API
- 支持自定义消息ID和超时控制
- 适合嵌入式设备和资源受限场景

#### 6.3.2 使用预编译客户端

部署包中已包含预编译的ARM 32-bit客户端：

```bash
# 拷贝客户端到目标板子
scp /home/cat/rkllm-server-deploy/client/rkllm_client \
    user@<其他ARM板子>:/home/user/
scp -r /home/cat/rkllm-server-deploy/client/lib \
    user@<其他ARM板子>:/home/user/

# 在目标板子上运行
cd /home/user
chmod +x rkllm_client

# 连接RK3576板子
./rkllm_client 192.168.137.204

# 或者指定端口
./rkllm_client 192.168.137.204 1883
```

#### 6.3.3 从源码编译

如需重新编译或修改客户端，可使用交叉编译脚本：

```bash
# 在开发机上编译
cd /home/cat/rkllm-server-deploy/client

# 执行交叉编译
./cross_compile.sh

# 编译产物
ls -la output/
# output/
# ├── rkllm_client          # ARM 32-bit可执行文件
# └── lib/                  # 依赖库
```

交叉编译配置：
- 编译器：`arm-linux-gnueabihf-gcc`
- 架构：ARM 32-bit (armhf)
- 依赖库：libmosquitto 1.6, libssl 1.0.0, libcrypto 1.0.0

#### 6.3.4 客户端源码说明

主要源文件：

| 文件 | 说明 |
|------|------|
| `rkllm_mqtt_client.h` | MQTT客户端头文件 |
| `rkllm_mqtt_client.c` | MQTT客户端实现 |
| `main.c` | 主程序入口 |
| `Makefile` | 编译配置 |
| `cross_compile.sh` | 交叉编译脚本 |

---

## 7. 外部集成调用示例

### 7.1 Node.js 调用

```javascript
const axios = require('axios');

const SERVER_URL = 'http://192.168.137.204:8080';

async function chat(messages, maxTokens = 100) {
    const response = await axios.post(
        `${SERVER_URL}/v1/chat/completions`,
        {
            messages,
            max_tokens: maxTokens
        },
        { timeout: 120000 }
    );
    return response.data.choices[0].message.content;
}

// 使用示例
(async () => {
    const messages = [
        { role: 'user', content: '你好，请用一句话介绍自己' }
    ];
    
    try {
        const reply = await chat(messages);
        console.log('AI回复:', reply);
    } catch (error) {
        console.error('调用失败:', error.message);
    }
})();
```

### 7.2 从OpenWebUI/LangChain等框架调用

#### OpenWebUI 配置

在 OpenWebUI 中添加自定义API：

```
设置 → 连接 → 添加连接
  名称: RK3576 Local
  URL:  http://192.168.137.204:8080
  API:  留空（无需认证）
```

#### LangChain 配置

```python
import requests

class RKLLMAPI:
    def __init__(self, base_url="http://192.168.137.204:8080"):
        self.base_url = base_url
    
    def invoke(self, messages, max_tokens=100):
        response = requests.post(
            f"{self.base_url}/v1/chat/completions",
            json={"messages": messages, "max_tokens": max_tokens}
        )
        return response.json()["choices"][0]["message"]["content"]
```

### 7.3 命令行测试

```bash
# 简单问答
curl -X POST http://192.168.137.204:8080/v1/chat/completions \
    -H "Content-Type: application/json" \
    -d '{"messages":[{"role":"user","content":"1+1等于几？"}],"max_tokens":10}'

# MQTT方式
mosquitto_pub -h 192.168.137.204 -t "rkllm/request/chat" \
    -d '{"request_id":"test1","messages":[{"role":"user","content":"1+1等于几？"}],"max_tokens":10}'
mosquitto_sub -h 192.168.137.204 -t "rkllm/response/test1" -C 1

# 运行测试脚本
cd /home/cat/rkllm-server-deploy/scripts
./test_api.sh                              # 默认测试5轮
TEST_ROUNDS=10 ./test_api.sh               # 测试10轮
SERVER_URL=http://192.168.137.204:8080 ./test_api_full.sh  # 全功能测试
```

---

## 8. 性能调优

### 8.1 频率锁定

```bash
# 自动执行（服务启动时）
# 手动执行
/home/cat/rkllm-server-deploy/scripts/fix_freq_rk3576.sh 2>/dev/null || true

# 锁定频率:
# CPU:  1.6GHz (最大性能模式)
# NPU:  950MHz (最大推理性能)
# GPU:  300MHz
# DDR:  528MHz
```

### 8.2 并发控制

当前实现使用**单线程锁** (`threading.Lock`) 保证请求串行处理：

- 优点：避免多进程竞争NPU资源，推理结果稳定
- 缺点：不支持并发请求，需排队处理

如需支持并发（实验性），可修改 `rkllm_server_v4.py`：
```python
# 移除锁限制，或使用信号量控制并发数
server_semaphore = threading.Semaphore(1)  # 最多1个并发推理
```

### 8.3 Token参数调优

| 参数 | 默认值 | 建议范围 | 说明 |
|------|--------|----------|------|
| max_tokens | 256 | 10-512 | 单次生成token数 |
| threads | 4 | 2-8 | CPU线程数 |

**调优建议**：
- 短问答场景: `max_tokens=20~50`，响应更快
- 长文本生成: `max_tokens=200~512`，内容更完整
- CPU密集场景: `threads=8`，充分利用多核

### 8.4 性能参考

基于 RK3576 + qwen3-1.7b-rk3576 模型的实测数据：

| 场景 | max_tokens | 响应时间 |
|------|-----------|----------|
| 简单问答 | 10 | ~2s |
| 知识问答 | 50 | ~5s |
| 长文本生成 | 100 | ~8s |
| 流式输出 | 50 | 实时逐字输出 |

---

## 9. 故障排查

### 9.1 常见问题

#### 服务无法启动

```bash
# 查看详细日志
journalctl -u rkllm-server -e --no-pager | tail -50
journalctl -u rkllm-mqtt-bridge -e --no-pager | tail -50

# 手动启动测试
python3 /home/cat/rkllm-server-deploy/src/rkllm_server_v4.py \
    --model /home/cat/ai/models/qwen3-1.7b-rk3576.rkllm \
    --llama-bin /home/cat/ai/rk-llama.cpp-rknpu2/build_rk3576/bin/llama-cli \
    --port 8080
```

#### 端口被占用

```bash
# 查找占用进程
ss -tlnp | grep 8080
ss -tlnp | grep 1883

# 停止占用进程
fuser -k 8080/tcp
fuser -k 1883/tcp
```

#### Mosquitto连接失败

```bash
# 检查Mosquitto状态
systemctl status mosquitto

# 重启Mosquitto
sudo systemctl restart mosquitto

# 测试连接
mosquitto_sub -h localhost -t "test" -C 1 &
mosquitto_pub -h localhost -t "test" -m "hello"
```

#### C/MQTT客户端运行失败

```bash
# 检查依赖库
ldd rkllm_client

# 检查库文件是否存在
ls -la lib/libmosquitto.so.1
ls -la lib/libssl.so.1.0.0
ls -la lib/libcrypto.so.1.0.0

# 设置库路径
export LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH
./rkllm_client 192.168.137.204
```

#### NPU Runtime 缺失

```bash
# 检查库文件
ls -la /usr/lib/librkllmrt.so

# 复制库文件
cp /home/cat/ai/rkllm-deploy/downloads/rknn-llm/rkllm-runtime/Linux/librkllm_api/aarch64/librkllmrt.so /usr/lib/
```

#### llama-cli 执行失败

```bash
# 直接测试llama-cli
/home/cat/ai/rk-llama.cpp-rknpu2/build_rk3576/bin/llama-cli \
    -m /home/cat/ai/models/qwen3-1.7b-rk3576.rkllm \
    -p "你好" -n 10 --simple-io
```

### 9.2 日志位置

```bash
# systemd 日志
journalctl -u rkllm-server -f
journalctl -u rkllm-mqtt-bridge -f

# 应用日志
tail -f /home/cat/rkllm-server-deploy/logs/service.log
tail -f /home/cat/rkllm-server-deploy/logs/service_error.log
tail -f /home/cat/rkllm-server-deploy/logs/bridge.log
tail -f /home/cat/rkllm-server-deploy/logs/bridge_error.log

# 脚本管理时的日志
./rkllm_service.sh logs 100
```

### 9.3 系统检查命令

```bash
# 检查NPU状态
cat /sys/class/devfreq/fdab0000.npu/cur_freq

# 检查CPU频率
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq

# 检查内存使用
free -h

# 检查进程
ps aux | grep -E "flask_server|llama|mqtt"

# 检查网络
ifconfig eth0
ping -c 3 192.168.1.1

# 检查MQTT连接
mosquitto_sub -v -t '#' -C 10 -h localhost
```

---

## 10. 安全与加固（可选）

### 10.1 服务访问控制

```bash
# 限制仅内网访问
# 修改 rkllm_server_v4.py 的 host 参数为 127.0.0.1
# 或配置防火墙
sudo iptables -A INPUT -p tcp --dport 8080 -s 192.168.0.0/16 -j ACCEPT
sudo iptables -A INPUT -p tcp --dport 8080 -j DROP

# MQTT访问控制
# 配置Mosquitto ACL
cat > /etc/mosquitto/conf.d/acl.conf << 'EOF'
acl_file /etc/mosquitto/acl
permissions -l
EOF

cat > /etc/mosquitto/acl << 'EOF'
user admin
topic write rkllm/#
topic read rkllm/#

user client
topic write rkllm/request/#
topic read rkllm/response/#
EOF
```

### 10.2 Nginx反向代理（可选）

```nginx
server {
    listen 80;
    server_name rkllm.local;

    location / {
        proxy_pass http://127.0.0.1:8080;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        
        # 超时设置
        proxy_read_timeout 180s;
        proxy_send_timeout 180s;
        
        # 流式输出支持
        proxy_buffering off;
        proxy_cache off;
    }
}
```

### 10.3 API认证（可选）

在 `rkllm_server_v4.py` 中添加认证中间件：

```python
from functools import wraps

def require_api_key(f):
    @wraps(f)
    def decorated(*args, **kwargs):
        api_key = request.headers.get('X-API-Key')
        if api_key != 'your-secret-key':
            return jsonify({"error": "Unauthorized"}), 401
        return f(*args, **kwargs)
    return decorated

@app.route('/v1/chat/completions', methods=['POST'])
@require_api_key
def chat_completions():
    ...
```

---

## 11. 附录

### 11.1 文件清单

| 文件路径 | 说明 |
|----------|------|
| `src/rkllm_server_v4.py` | Flask服务器主程序 |
| `scripts/rkllm_mqtt_bridge.py` | MQTT桥接服务 |
| `scripts/rkllm_service.sh` | 服务管理脚本 |
| `scripts/rkllm_client.py` | Python MQTT客户端 |
| `scripts/rkllm_client_fixed.sh` | Shell MQTT客户端 |
| `configs/rkllm-server.service` | Flask服务配置 |
| `configs/rkllm-mqtt-bridge.service` | MQTT桥接服务配置 |
| `client/rkllm_client` | ARM 32位C/MQTT客户端 |
| `client/rkllm_mqtt_client.c` | C客户端源码 |
| `client/cross_compile.sh` | 交叉编译脚本 |
| `client/lib/*.so` | 客户端依赖库 |
| `install.sh` | 一键安装脚本 |
| `create_run_package.sh` | run包生成脚本 |

### 11.2 端口与地址

| 项目 | 值 |
|------|-----|
| HTTP服务地址 | `http://192.168.137.204:8080` |
| HTTP监听地址 | `0.0.0.0` (所有网卡) |
| HTTP端口 | `8080` |
| 协议 | HTTP |
| MQTT Broker地址 | `192.168.137.204:1883` |
| MQTT端口 | `1883` |
| 协议 | MQTT 3.1.1 |

### 11.3 MQTT主题参考

| 主题模式 | 方向 | 说明 |
|----------|------|------|
| `rkllm/request/health` | 请求 | 健康检查 |
| `rkllm/request/models` | 请求 | 模型列表 |
| `rkllm/request/chat` | 请求 | 聊天请求 |
| `rkllm/request/generate` | 请求 | 生成请求 |
| `rkllm/response/<id>` | 响应 | 对应请求的响应 |

### 11.4 相关链接

| 资源 | 链接 |
|------|------|
| RKLLM SDK文档 | `/share/rk3576/ai/rkllm-deploy/downloads/rknn-llm/doc/Rockchip_RKLLM_SDK_CN_1.3.0.pdf` |
| llama.cpp项目 | `https://github.com/ggerganov/llama.cpp` |
| Flask文档 | `https://flask.palletsprojects.com/` |
| Mosquitto文档 | `https://mosquitto.org/documentation/` |

### 11.5 部署包信息

| 项目 | 说明 |
|------|------|
| 部署包名称 | `rkllm_deploy_v1.0.0.run` |
| 部署包大小 | 3.8MB |
| 包含内容 | 服务程序、脚本、配置、C客户端、依赖库 |
| 支持系统 | Ubuntu 22.04 LTS (ARM64) |

---

## 版本信息

- **文档版本**: 2.1
- **适用硬件**: RK3576
- **适用软件**: RKLLM Runtime 1.3.0
- **模型**: qwen3-1.7b-rk3576.rkllm
- **新增功能**: MQTT桥接服务、C/MQTT客户端、一键部署run包、PQ AI推理服务器
- **更新时间**: 2026-08-11

---

## 12. PQ AI Terminal 双机协作集成

### 12.1 系统拓扑

```
┌─────────────── 开发 PC (Windows) ───────────────┐
│                                                   │
│  以太网2 (192.168.137.x)                         │  以太网5 (192.168.14.x)
│      ↓                                            │      ↓
│  RK3576 算力卡                                 T536 采集终端
│  192.168.137.204                               192.168.14.101
│                                                  │
│  ┌──────────────────────────────────────┐        │
│  │  Flask LLM Server (port 8080)        │        │
│  │  - 大模型对话/文本生成                 │        │
│  │  - 延迟: 20-35s (首次推理)           │        │
│  └──────────────────────────────────────┘        │
│  ┌──────────────────────────────────────┐        │
│  │  PQ AI Inference Server (port 9090)  │        │
│  │  - 实时 PQ 异常评分                   │        │
│  │  - 延迟: 1-11ms                      │        │
│  │  - 协议: 与 ai_rpc.c 兼容             │        │
│  └──────────────────────────────────────┘        │
│  ┌──────────────────────────────────────┐        │
│  │  MQTT Bridge (port 1883)             │        │
│  └──────────────────────────────────────┘        │
│      ↑                                            │      ↑
│      └──── USB 虚拟网卡 (RNDIS) ──────────────────┘      │
│           RK3576 usb0: 192.168.100.1/24                  │
│           T536 usb0:  192.168.100.2/24                  │
│                                                   │
└───────────────────────────────────────────────────┘
```

### 12.2 双服务架构说明

| 服务 | 端口 | 协议 | 用途 | 延迟 |
|------|------|------|------|------|
| Flask LLM Server | 8080 | HTTP ChatCompletions | 大模型对话、增强分析 | 20-35s |
| PQ AI Inference Server | 9090 | 自定义 TCP JSON | 实时 PQ 异常评分 | 1-11ms |
| MQTT Broker | 1883 | MQTT 3.1.1 | IoT 消息桥接 | <1ms |

**两种服务的定位不同**：
- **Flask LLM Server**：用于电能质量事件的自然语言解释、治理建议生成等需要大模型的场景
- **PQ AI Inference Server**：用于实时 PQ 异常检测评分（iForest/AE/CNN1D），嵌入 pq_sim/pq_terminal 运行时调用

### 12.3 PQ AI Inference Server 协议

**请求**（与 `pq_ai_terminal/ai/ai_rpc.c` 的 `build_request` 格式一致）：
```json
{"cmd":"infer","features":[0.1,0.2,...],"vthd":3.5,"ithd":12.0}
```

**应答**（与 `pq_ai_terminal/ai/ai_rpc.c` 的 `parse_response` 格式一致）：
```json
{"if":0.6732,"ae":0.7102,"cls":1,"conf":1.0,"lat":1}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| if | float | iForest 异常得分 (0-1) |
| ae | float | 自编码器异常得分 (0-1) |
| cls | int | CNN 分类 (0=正常, 1-6=PQ 事件类型) |
| conf | float | 分类置信度 (0-1) |
| lat | int | 推理耗时 (ms) |

### 12.4 部署与验证结果

**验证时间**：2026-08-11

**硬件环境**：
| 设备 | IP | 操作系统 | 连接方式 |
|------|-----|---------|---------|
| RK3576 | 192.168.137.204 | Ubuntu 22.04 (ARM64) | 以太网2 + USB RNDIS |
| T536 | 192.168.14.101 | Linux 5.10.198 (ARM64) | 以太网5 + USB RNDIS |

**USB 虚拟网卡状态**：
| 设备 | 接口 | IP | 状态 | 延迟 |
|------|------|-----|------|------|
| RK3576 | usb0 | 192.168.100.1/24 | UP, LOWER_UP | 0.6-2.2ms |
| T536 | usb0 | 192.168.100.2/24 | UP, LOWER_UP | 0.7-1.1ms |

**AI 推理服务验证**：
| 测试项 | 结果 | 详情 |
|--------|------|------|
| Flask /health | ✅ | status=ready, uptime=73046s |
| Flask /v1/models | ✅ | qwen3-1.7b-rk3576.rkllm |
| Flask AI 推理 | ✅ | 22.87s (首次加载) |
| T536→RK3576 AI 推理 | ✅ | 35.85s (通过 USB 网卡) |
| PQ Inference Server | ✅ | 自测试 PASS，延迟 1ms |
| T536→RK3576 PQ 推理 | ✅ | 14/14 请求成功，延迟 1-11ms |
| 压力测试 (10次) | ✅ | 10/10 成功，延迟 2-11ms |

**PQ AI Terminal 协议兼容性验证**：
- 请求格式：`{"cmd":"infer","features":[...],"vthd":x,"ithd":y}` ✅
- 应答格式：`{"if":...,"ae":...,"cls":...,"conf":...,"lat":...}` ✅
- 字段完整性：if, ae, cls, conf, lat 全部返回 ✅
- 取值范围：所有字段在合法范围内 ✅
- 往返延迟：1-11ms，远低于 ai_rpc.c 的 2000ms 超时阈值 ✅

### 12.5 PQ AI Inference Server 部署

#### 12.5.1 文件位置

```
/home/cat/rkllm-server-deploy/
├── scripts/
│   ├── rk3576_inference_server.py   # PQ 推理服务器主程序
│   ├── rkllm_mqtt_bridge.py         # MQTT 桥接
│   └── llama_wrapper.py             # llama-cli 封装
├── rkllm_server/
│   ├── flask_server.py              # Flask LLM 服务器
│   └── lib/
│       └── librkllmrt.so            # RKLLM 运行时
└── logs/
    ├── service.log                  # Flask 日志
    └── inference_server.log         # PQ 推理日志
```

#### 12.5.2 启动 PQ AI Inference Server

```bash
# 前台运行（调试）
python3 /home/cat/rkllm-server-deploy/scripts/rk3576_inference_server.py \
    --host 192.168.100.1 --port 9090

# 后台运行
nohup python3 /home/cat/rkllm-server-deploy/scripts/rk3576_inference_server.py \
    --host 192.168.100.1 --port 9090 \
    > /home/cat/rkllm-server-deploy/logs/inference_server.log 2>&1 &

# 自测试
python3 /home/cat/rkllm-server-deploy/scripts/rk3576_inference_server.py --test
```

#### 12.5.3 systemd 服务

服务文件已安装：`/etc/systemd/system/rk3576_pq_ai_inference.service`

```bash
# 启动
sudo systemctl start rk3576_pq_ai_inference

# 设置开机自启
sudo systemctl enable rk3576_pq_ai_inference

# 查看状态
sudo systemctl status rk3576_pq_ai_inference

# 查看日志
journalctl -u rk3576_pq_ai_inference -f
tail -f /home/cat/rkllm-server-deploy/logs/inference_server.log
```

#### 12.5.4 从 T536 调用 AI 推理

```bash
# 方法1: Python 脚本
python3 /tmp/test_pq_protocol.py 192.168.100.1 9090

# 方法2: 直接 TCP 调用
echo '{"cmd":"infer","features":[0.1,0.2,0.3],"vthd":3.5,"ithd":12.0}' | \
    nc -q 1 192.168.100.1 9090

# 方法3: C 代码 (ai_rpc.c 已内置)
# ai_rpc_init("192.168.100.1", 9090);
# ai_rpc_infer(&feat, &metrics, &result);
```

### 12.6 pq_ai_terminal 集成指南

#### 12.6.1 修改 config.ini

```ini
[compute_module]
; 算力模组开关 (0=仿真模式, 1=真实硬件模式)
enabled = 1
; RK3576 通过 USB 虚拟网卡的 IP
ip = 192.168.100.1
; PQ AI 推理端口 (与 rk3576_inference_server.py 一致)
port = 9090
; 算力模组类型 (flask=LLM服务器, pq_infer=实时推理服务器)
type = pq_infer

[t536_terminal]
ip = 192.168.14.101
ssh_port = 8888
user = csg
password = Iot@csg123
```

#### 12.6.2 交叉编译 pq_sim

在 PC（WSL Ubuntu）上交叉编译：

```bash
# 安装交叉编译器
sudo apt install gcc-aarch64-linux-gnu

# 编译
cd pq_ai_terminal
make CROSS_CC=aarch64-linux-gnu-gcc CROSS_CFLAGS="-std=c99 -Wall -Wextra -O2" linux-arm

# 产物
ls -la pq_terminal_arm
```

在 T536 上运行：

```bash
# 拷贝到 T536
scp -P 8888 pq_terminal_arm csg@192.168.14.101:/home/csg/

# 在 T536 上运行
ssh -p 8888 csg@192.168.14.101
chmod +x pq_terminal_arm
./pq_terminal_arm --config config.ini
```

### 12.7 可行性评估

| 维度 | 评估 | 说明 |
|------|------|------|
| **硬件就绪** | ✅ 可行 | RK3576 (6.1.99-rk3576) + T536 (5.10.198) + USB RNDIS 链路全部正常 |
| **USB 通信** | ✅ 可行 | 双向 ping 成功，延迟 0.9-3.5ms，稳定可靠 |
| **AI 推理** | ✅ 可行 | RK3576 NPU 工作正常，PQ 推理延迟 1-11ms，LLM 推理延迟 20-35s |
| **协议兼容** | ✅ 可行 | rk3576_inference_server.py 与 ai_rpc.c 协议 100% 兼容 |
| **交叉编译** | ✅ 可行 | Ubuntu 22.04 + GCC Linaro 11.3.1 成功编译 aarch64 ELF (51KB) |
| **嵌入式运行** | ✅ 已验证 | pq_terminal_arm 在 T536 上成功运行，依赖库 libc/libm 均满足 |
| **全链路验证** | ✅ 已验证 | T536→USB ECM→RK3576 AI RPC→结果返回 全链路 100% 成功 |
| **长期稳定** | ⚠️ 待验证 | 需进行长时间压力测试和温度稳定性测试 |

**结论**：**PQ AI Terminal 软件仿真与真实硬件运行环境完全可行**。

已验证的核心路径：
1. T536 (采样/指标/事件) → USB ECM (RTT 1-9ms) → RK3576 (AI 推理) → 返回结果 ✅
2. 协议兼容：ai_rpc.c ↔ rk3576_inference_server.py ✅
3. 实时性：PQ 推理 2-9ms，远低于 2s 超时阈值 ✅
4. 降级机制：ai_rpc.c 的 fallback 机制在 RK3576 离线时自动启用 ✅
5. 交叉编译：GCC Linaro 11.3.1 → aarch64 ELF，动态链接 libc.so.6 + libm.so.6 ✅
6. 真实硬件模式：config.ini 配置 `compute_module.ip=192.168.100.1` 跳过本地仿真器 ✅
7. PQ 指标检测：S2 场景电压 THD 6.89%、电流 THD 16.40% 正确报警 ✅
8. AI 推理结果：IF=0.768, AE=0.707, CNN=1(EV charging), 置信度 1.0 ✅
9. MQTT 告警：事件上报正常 ✅

**T536 运行实测数据**：
```
场景: S2-充电桩接入 (80kW, 5/7/11/13次谐波)
周期数: 5
AI RPC 成功率: 5/5 (100%)
推理延迟: 2-9ms (平均 4ms)
USB ECM RTT: 1-9ms
电压 THD: 6.892% (阈值 5%) → ALARM ✅
电流 THD: 16.401% (阈值 8%) → ALARM ✅
IF 异常得分: 0.7684
AE 异常得分: 0.7072
CNN 分类: 1 (EV charging) 置信度 1.000
```

**交叉编译产物**：
```
文件: pq_terminal_arm
大小: 51864 字节
架构: ARM aarch64
格式: ELF 64-bit LSB executable
依赖: libm.so.6, libc.so.6
编译器: GCC Linaro 11.3.1-2022.06
```

**后续工作**：
1. 长时间稳定性测试（> 24h 连续推理）
2. NPU 温度和功耗监测
3. RKNN 量化模型部署（替换 Python 规则推理）
4. T536 实际采集 → RK3576 AI 推理 → 反馈闭环验证
5. 多场景压力测试（S1-S5 连续运行）
