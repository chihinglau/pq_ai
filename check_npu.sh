#!/bin/bash
# RK3576 NPU 状态监控脚本
# 用于查看 NPU 运行状态和负载

echo "========================================"
echo " RK3576 NPU 状态监控"
echo "========================================"
echo ""

# 1. 检查 NPU 驱动状态
echo "[1] NPU 驱动状态:"
dmesg | grep -i rknpu | tail -3
echo ""

# 2. 检查 NPU 设备节点
echo "[2] NPU 设备节点:"
ls -la /dev/rknpu* 2>/dev/null || echo "  (无 /dev/rknpu* 节点)"
ls -la /sys/devices/platform/rknpu_dev*/ 2>/dev/null | head -5
echo ""

# 3. 检查 RKNN 运行时库
echo "[3] RKNN 运行时版本:"
python3 -c "from rknnlite.api import RKNNLite; print('  RKNNLite: OK')" 2>/dev/null
if [ $? -ne 0 ]; then
    echo "  RKNNLite: 未安装"
fi
echo ""

# 4. 检查 NPU 服务进程
echo "[4] NPU AI 服务状态:"
ps aux | grep wave_inference | grep -v grep
echo ""

# 5. 检查 RKLLM 服务
echo "[5] RKLLM 服务状态:"
systemctl status rkllm 2>/dev/null | grep -E "Active|running" || echo "  RKLLM 未运行"
echo ""

# 6. NPU 推理性能测试
echo "[6] NPU 快速推理测试:"
python3 << 'PYEOF' 2>/dev/null
from rknnlite.api import RKNNLite
import numpy as np
import time

rknn = RKNNLite()
ret = rknn.load_rknn("/home/cat/pq_ai_v3/models/cnn1d_8class.rknn")
if ret != 0:
    print("  模型加载失败")
    exit(1)

ret = rknn.init_runtime(core_mask=2)  # core1
if ret != 0:
    print("  运行时初始化失败")
    exit(1)

test_input = np.random.randn(1, 3, 256).astype(np.float32)
times = []
for i in range(10):
    start = time.perf_counter()
    outputs = rknn.inference(inputs=[test_input])
    ms = (time.perf_counter() - start) * 1000
    times.append(ms)

avg_ms = sum(times) / len(times)
print(f"  10次推理平均: {avg_ms:.2f}ms")
print(f"  模型输出形状: {outputs[0].shape}")
print(f"  NPU 状态: ✓ 正常工作")

rknn.release()
PYEOF
echo ""

echo "========================================"
echo " 监控完成"
echo "========================================"
