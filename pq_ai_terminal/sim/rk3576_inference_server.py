#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
PQ AI Terminal 推理服务器 (RK3576 侧)
实现与 pq_ai_terminal/ai_rpc.c 兼容的 TCP 协议：
  - 请求: {"cmd":"infer","features":[f1,f2,...],"vthd":x,"ithd":y}
  - 应答: {"if":score,"ae":score,"cls":class,"conf":conf,"lat":ms}

可选: 当 RK3576 Flask LLM 服务可用时，可增强分析

作者: PQ AI Terminal Team
日期: 2026-08-11
"""

import os
import sys
import json
import time
import socket
import struct
import threading
import signal
import argparse
import subprocess
import math

# ==================== 配置 ====================
DEFAULT_HOST = "0.0.0.0"
DEFAULT_PORT = 9090
LLM_SERVER_URL = "http://localhost:8080"
FEATURE_COUNT = 27  # IF_N_FEATURES

# ==================== AI 模型 (Python 实现) ====================

def compute_iforest_score(features):
    """iForest 异常检测得分 (0-1, 越高越异常)"""
    n = len(features)
    if n == 0:
        return 0.0
    mean = sum(features) / n
    variance = sum((x - mean) ** 2 for x in features) / n
    std = math.sqrt(variance) if variance > 0 else 0.001

    # 基于统计特征的异常评分
    cv = std / (abs(mean) + 0.001)  # 变异系数
    peak = max(abs(x) for x in features)
    rms = math.sqrt(sum(x ** 2 for x in features) / n)

    # 归一化: cv 和 peak 越高越异常
    score = min(1.0, 0.4 * min(cv / 2.0, 1.0) + 0.3 * min(peak / (rms + 0.001), 1.0) + 0.3 * min(abs(mean) / (rms + 0.001), 1.0))
    return round(max(0.0, min(1.0, score)), 4)


def compute_ae_score(features):
    """自编码器异常得分 (0-1, 越高越异常)"""
    n = len(features)
    if n == 0:
        return 0.0
    mean = sum(features) / n

    # 简化的 AE: 偏离均值的程度
    deviation = sum(abs(x - mean) for x in features) / n
    rms = math.sqrt(sum(x ** 2 for x in features) / n)

    # 归一化
    score = min(1.0, deviation / (rms + 0.001) * 0.5 + 0.5 * min(rms / (abs(mean) + 0.001), 1.0))
    return round(max(0.0, min(1.0, score)), 4)


def compute_cnn_class(features, vthd=0.0, ithd=0.0):
    """1D-CNN 分类 (7 类: 0=normal, 1-6=各类PQ事件)"""
    n = len(features)
    if n == 0:
        return 0, 0.0

    # 基于特征规则的分类 (实际应训练 CNN, 此处用规则模拟)
    # 特征统计
    mean = sum(features) / n
    variance = sum((x - mean) ** 2 for x in features) / n
    rms = math.sqrt(sum(x ** 2 for x in features) / n)
    peak = max(abs(x) for x in features)
    thd_v = vthd
    thd_i = ithd

    # 7 类: 0=正常, 1=谐波, 2=电压暂降, 3=电压暂升, 4=电压闪变, 5=三相不平衡, 6=工频偏差
    scores = [0.0] * 7

    # 类 0: 正常
    scores[0] = max(0.0, 1.0 - 0.3 * (thd_v / 5.0 + thd_i / 8.0))

    # 类 1: 谐波 (THD 高)
    scores[1] = min(1.0, 0.5 * (thd_v / 5.0) + 0.5 * (thd_i / 8.0))

    # 类 2: 电压暂降 (电压特征异常低)
    v_ratio = abs(mean) / (rms + 0.001)
    scores[2] = min(1.0, max(0.0, 1.0 - v_ratio)) if thd_v > 2.0 else 0.0

    # 类 3: 电压暂升 (电压特征异常高)
    scores[3] = min(1.0, max(0.0, v_ratio - 1.2)) if thd_v > 1.0 else 0.0

    # 类 4: 电压闪变 (特征波动大)
    if n > 1:
        diffs = [abs(features[i] - features[i-1]) for i in range(1, n)]
        flicker = sum(diffs) / len(diffs)
        scores[4] = min(1.0, flicker / (rms + 0.001))

    # 类 5: 三相不平衡
    if n >= 3:
        phases = [abs(features[i]) for i in range(min(3, n))]
        if len(phases) >= 2 and max(phases) > 0:
            imbalance = (max(phases) - min(phases)) / max(phases)
            scores[5] = min(1.0, imbalance)

    # 类 6: 工频偏差
    scores[6] = min(1.0, variance / (rms + 0.001) * 0.3)

    # 选最高分
    max_idx = max(range(7), key=lambda i: scores[i])
    max_conf = min(1.0, scores[max_idx] + 0.1)

    return max_idx, round(max_conf, 4)


def run_inference(features, vthd=0.0, ithd=0.0):
    """执行完整 AI 推理"""
    t0 = time.time()

    if_score = compute_iforest_score(features)
    ae_score = compute_ae_score(features)
    cnn_class, cnn_conf = compute_cnn_class(features, vthd, ithd)

    t1 = time.time()
    latency_ms = int((t1 - t0) * 1000)

    return {
        "if": if_score,
        "ae": ae_score,
        "cls": cnn_class,
        "conf": cnn_conf,
        "lat": latency_ms
    }


# ==================== 请求解析 ====================

def parse_infer_request(data):
    """解析推理请求 JSON"""
    try:
        req = json.loads(data)
    except json.JSONDecodeError:
        return None

    if req.get("cmd") != "infer":
        return None

    features = req.get("features", [])
    if not isinstance(features, list):
        return None

    # 转换为 float
    features = [float(f) for f in features[:FEATURE_COUNT]]

    vthd = float(req.get("vthd", 0.0))
    ithd = float(req.get("ithd", 0.0))

    return features, vthd, ithd


def build_response(result):
    """构建应答 JSON"""
    return json.dumps(result, separators=(",", ":"))


# ==================== LLM 增强分析 (可选) ====================

def llm_enhanced_analysis(features, vthd, ithd, basic_result):
    """调用本地 Flask LLM 进行增强分析"""
    try:
        import urllib.request
        prompt = f"""你是电能质量分析助手。请根据以下数据给出简要分析：
- 电压THD: {vthd:.2f}%
- 电流THD: {ithd:.2f}%
- iForest异常得分: {basic_result['if']:.4f}
- AE异常得分: {basic_result['ae']:.4f}
- CNN分类: {basic_result['cls']} (置信度: {basic_result['conf']:.4f})

请给出1-2句话的分析结论。"""

        payload = json.dumps({
            "model": "qwen3-1.7b-rk3576.rkllm",
            "messages": [{"role": "user", "content": prompt}],
            "max_tokens": 128
        }).encode()

        req = urllib.request.Request(
            f"{LLM_SERVER_URL}/v1/chat/completions",
            data=payload,
            headers={"Content-Type": "application/json"}
        )
        with urllib.request.urlopen(req, timeout=30) as resp:
            data = json.loads(resp.read())
            return data.get("choices", [{}])[0].get("message", {}).get("content", "")
    except Exception as e:
        return ""


# ==================== TCP 服务器 ====================

class PQAIInferenceServer:
    def __init__(self, host, port, enable_llm=False):
        self.host = host
        self.port = port
        self.enable_llm = enable_llm
        self.server_sock = None
        self.running = False
        self.total_requests = 0
        self.total_errors = 0

    def start(self):
        """启动 TCP 服务器"""
        self.server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_sock.bind((self.host, self.port))
        self.server_sock.listen(5)
        self.server_sock.settimeout(1.0)
        self.running = True

        print(f"[PQ AI Server] Listening on {self.host}:{self.port}")
        print(f"[PQ AI Server] LLM enhanced: {'yes' if self.enable_llm else 'no'}")
        print(f"[PQ AI Server] Ready for inference requests...")

        while self.running:
            try:
                client_sock, addr = self.server_sock.accept()
                print(f"[PQ AI Server] Client connected from {addr[0]}:{addr[1]}")
                thread = threading.Thread(target=self.handle_client, args=(client_sock, addr))
                thread.daemon = True
                thread.start()
            except socket.timeout:
                continue
            except OSError:
                break

    def stop(self):
        """停止服务器"""
        self.running = False
        if self.server_sock:
            self.server_sock.close()
        print(f"\n[PQ AI Server] Stopped. Stats: {self.total_requests} requests, {self.total_errors} errors")

    def handle_client(self, client_sock, addr):
        """处理单个客户端连接"""
        try:
            client_sock.settimeout(30.0)
            buf = b""

            while self.running:
                try:
                    chunk = client_sock.recv(4096)
                    if not chunk:
                        break
                    buf += chunk

                    # 尝试解析请求
                    try:
                        data = buf.decode("utf-8")
                        parsed = parse_infer_request(data)

                        if parsed is not None:
                            features, vthd, ithd = parsed
                            self.total_requests += 1

                            # 执行推理
                            result = run_inference(features, vthd, ithd)

                            # 可选: LLM 增强
                            if self.enable_llm:
                                llm_analysis = llm_enhanced_analysis(features, vthd, ithd, result)
                                if llm_analysis:
                                    result["llm_note"] = llm_analysis

                            # 发送应答
                            response = build_response(result)
                            client_sock.sendall(response.encode("utf-8"))
                            print(f"  [INFER #{self.total_requests}] "
                                  f"if={result['if']:.4f} ae={result['ae']:.4f} "
                                  f"cls={result['cls']} conf={result['conf']:.4f} "
                                  f"lat={result['lat']}ms "
                                  f"(feat={len(features)}, vthd={vthd:.2f}, ithd={ithd:.2f})")

                            buf = b""  # reset for next request
                        elif buf.endswith(b"\n") or len(buf) > 65536:
                            # 错误请求
                            self.total_errors += 1
                            error_resp = json.dumps({"error": "bad request", "reason": "parse failed"})
                            client_sock.sendall(error_resp.encode("utf-8"))
                            print(f"  [ERROR] Bad request from {addr}")
                            buf = b""
                    except (json.JSONDecodeError, UnicodeDecodeError):
                        # 可能是不完整的请求，等待更多数据
                        if len(buf) > 65536:
                            self.total_errors += 1
                            error_resp = json.dumps({"error": "request too large"})
                            client_sock.sendall(error_resp.encode("utf-8"))
                            buf = b""

                except socket.timeout:
                    break

        except Exception as e:
            print(f"  [ERROR] Client handler error: {e}")
        finally:
            client_sock.close()
            print(f"[PQ AI Server] Client {addr} disconnected")


# ==================== 主入口 ====================

def main():
    parser = argparse.ArgumentParser(description="PQ AI Inference Server for RK3576")
    parser.add_argument("--host", default=DEFAULT_HOST, help="Listen host (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="Listen port (default: 9090)")
    parser.add_argument("--enable-llm", action="store_true", help="Enable LLM enhanced analysis")
    parser.add_argument("--test", action="store_true", help="Run a quick self-test")
    args = parser.parse_args()

    if args.test:
        # 自测试
        print("[Self-Test] Running inference test...")
        test_features = [float(i) * 0.1 for i in range(1, FEATURE_COUNT + 1)]
        result = run_inference(test_features, vthd=3.5, ithd=12.0)
        print(f"  Features: {len(test_features)} values")
        print(f"  Result: {json.dumps(result, indent=2)}")
        assert "if" in result, "Missing if score"
        assert "ae" in result, "Missing ae score"
        assert "cls" in result, "Missing cls"
        assert "conf" in result, "Missing conf"
        assert "lat" in result, "Missing latency"
        assert result["lat"] < 1000, f"Latency too high: {result['lat']}ms"
        print("[Self-Test] PASSED ✓")
        return

    # 正式运行
    server = PQAIInferenceServer(args.host, args.port, args.enable_llm)

    def signal_handler(sig, frame):
        print("\n[PQ AI Server] Shutting down...")
        server.stop()
        sys.exit(0)

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    server.start()


if __name__ == "__main__":
    main()
