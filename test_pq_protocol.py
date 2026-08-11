#!/usr/bin/env python3
"""
测试 pq_ai_terminal AI RPC 协议兼容性
模拟 ai_rpc.c 的 USB ECM TCP 请求-应答流程

请求: {"cmd":"infer","features":[f1,f2,...],"vthd":x,"ithd":y}
期望应答: {"if":score,"ae":score,"cls":class,"conf":conf,"lat":ms}
"""

import socket
import json
import time
import sys

def test_inference(host, port, features, vthd, ithd):
    """模拟 ai_rpc_infer 的请求流程"""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5.0)

    try:
        sock.connect((host, port))
    except Exception as e:
        print(f"  [FAIL] Connect to {host}:{port} failed: {e}")
        return False

    # 构建请求 (与 ai_rpc.c 的 build_request 格式一致)
    request = {
        "cmd": "infer",
        "features": [round(f, 4) for f in features],
        "vthd": round(vthd, 3),
        "ithd": round(ithd, 3)
    }
    req_str = json.dumps(request, separators=(",", ":"))

    t0 = time.time()
    try:
        sock.sendall(req_str.encode("utf-8"))
        response = sock.recv(4096)
        t1 = time.time()
    except Exception as e:
        print(f"  [FAIL] Send/Recv failed: {e}")
        sock.close()
        return False

    sock.close()
    latency_ms = int((t1 - t0) * 1000)

    # 解析应答 (与 ai_rpc.c 的 parse_response 格式一致)
    try:
        result = json.loads(response.decode("utf-8"))
    except json.JSONDecodeError:
        print(f"  [FAIL] Invalid JSON response: {response[:100]}")
        return False

    # 验证字段
    required = ["if", "ae", "cls", "conf", "lat"]
    missing = [k for k in required if k not in result]
    if missing:
        print(f"  [FAIL] Missing fields: {missing}")
        print(f"  Response: {response[:200]}")
        return False

    # 验证范围
    assert 0.0 <= result["if"] <= 1.0, f"if_score out of range: {result['if']}"
    assert 0.0 <= result["ae"] <= 1.0, f"ae_score out of range: {result['ae']}"
    assert 0 <= result["cls"] <= 6, f"cnn_class out of range: {result['cls']}"
    assert 0.0 <= result["conf"] <= 1.0, f"confidence out of range: {result['conf']}"
    assert isinstance(result["lat"], int), f"latency not int: {type(result['lat'])}"

    print(f"  [OK] if={result['if']:.4f} ae={result['ae']:.4f} "
          f"cls={result['cls']} conf={result['conf']:.4f} "
          f"lat={result['lat']}ms (roundtrip={latency_ms}ms)")
    return True


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 9090

    print(f"=== PQ AI Terminal Protocol Compatibility Test ===")
    print(f"Target: {host}:{port}")
    print()

    # 测试用例
    test_cases = [
        ("正常波形", [0.1] * 27, 1.0, 1.0),
        ("轻度畸变", [0.5] * 27, 3.5, 8.0),
        ("严重畸变", [1.0] * 27, 6.0, 15.0),
        ("混合特征", [float(i % 5 + 1) * 0.2 for i in range(27)], 2.0, 5.0),
    ]

    passed = 0
    failed = 0

    for name, features, vthd, ithd in test_cases:
        print(f"\n  Test: {name} (vthd={vthd}, ithd={ithd})")
        if test_inference(host, port, features, vthd, ithd):
            passed += 1
        else:
            failed += 1

    # 压力测试
    print(f"\n  Stress test: 10 consecutive requests...")
    for i in range(10):
        features = [float(i % 7 + 1) * 0.15 for i in range(27)]
        if test_inference(host, port, features, 2.5, 6.0):
            passed += 1
        else:
            failed += 1

    print(f"\n=== Summary: {passed} passed, {failed} failed ===")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
