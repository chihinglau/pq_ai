#!/bin/bash
# 启动 RK3576 推理服务器

# 杀掉旧进程
pkill -f rk3576_inference_server 2>/dev/null
sleep 1

# 启动新进程
python3 /home/cat/rkllm-server-deploy/scripts/rk3576_inference_server.py \
    --host 192.168.100.1 \
    --port 9090 \
    > /home/cat/rkllm-server-deploy/logs/inference_server.log 2>&1 &

echo "Server PID: $!"
sleep 2
echo "--- Log ---"
cat /home/cat/rkllm-server-deploy/logs/inference_server.log
