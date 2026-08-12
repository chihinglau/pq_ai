#!/usr/bin/env python3
"""
PC端RK3576模拟服务端
用于在PC上本地模拟RK3576的AI推理服务，方便调试

功能:
1. 接收T536/测试客户端发送的原始波形
2. 解析波形数据
3. 提取特征
4. AI推理（简化版）
5. 返回推理结果

用法:
    python3 wave_inference_server_sim.py [--host HOST] [--port PORT]
    
示例:
    # 本地测试
    python3 wave_inference_server_sim.py --host 127.0.0.1 --port 9090
    
    # 局域网测试（让T536访问）
    python3 wave_inference_server_sim.py --host 0.0.0.0 --port 9090
"""

import socket
import struct
import time
import sys
import math
from datetime import datetime

# ========== 协议常量 ==========
PROTO_MAGIC = 0x57415645  # "WAVE"
PROTO_VERSION = 1
CMD_SEND_WAVEFORM = 1
CMD_RESPONSE = 2
RESP_OK = 0
RESP_ERROR = 1

# 协议头格式
PROTO_HEADER_FORMAT = '!IbbHIIII'
PROTO_HEADER_SIZE = struct.calcsize(PROTO_HEADER_FORMAT)

# AI响应格式 (对应C端ai_response_t结构)
# magic(4) + resp_type(1) + reserved(3) + if_score(4) + ae_score(4) + 
# cnn_class(4) + cnn_confidence(4) + latency_ms(4) + result_code(4) + result_desc(16) = 48字节
AI_RESPONSE_FORMAT = '!Ib3sffifIi16s'
AI_RESPONSE_SIZE = struct.calcsize(AI_RESPONSE_FORMAT)

# 波形参数
WS_SINGLE_WAVEFORM_SIZE = 7182
WS_POINTS_PER_CYCLE = 256
WS_CHANNEL_COUNT = 7

CHANNEL_NAMES = ['UA', 'UB', 'UC', 'IA', 'IB', 'IC', 'IZ']


def read_le32(data, offset):
    """读取小端序32位整数"""
    return (int(data[offset]) |
            (int(data[offset + 1]) << 8) |
            (int(data[offset + 2]) << 16) |
            (int(data[offset + 3]) << 24))


def read_le_float(data, offset):
    """读取小端序32位浮点数"""
    return struct.unpack('<f', data[offset:offset+4])[0]


def parse_waveform(wave_data):
    """
    解析T536发送的原始波形数据
    """
    channels = [[] for _ in range(WS_CHANNEL_COUNT)]
    frame_seq = None
    frames_found = 0
    idx = 0
    
    while idx < len(wave_data) - 1:
        # 查找帧头 0x68 0x36
        if wave_data[idx] == 0x68 and wave_data[idx + 1] == 0x36:
            # 检查数据长度是否足够
            if idx + WS_SINGLE_WAVEFORM_SIZE + 7 > len(wave_data):
                break
            
            frame_len = read_le32(wave_data, idx + 2)
            frame_seq_byte = wave_data[idx + 6]
            wave_start = idx + 7
            
            # 读取周波序号
            cycle_seq = read_le32(wave_data, wave_start)
            frames_found += 1
            
            # 跳过周波序号(4) + 时间戳(10) = 14字节
            data_offset = wave_start + 14
            
            # 提取7通道数据
            for ch in range(WS_CHANNEL_COUNT):
                for pt in range(WS_POINTS_PER_CYCLE):
                    val = read_le_float(wave_data, data_offset)
                    channels[ch].append(val)
                    data_offset += 4
            
            frame_seq = cycle_seq
            idx += WS_SINGLE_WAVEFORM_SIZE + 7
        else:
            idx += 1
    
    return channels, frame_seq, frames_found


def calc_rms(values):
    """计算RMS"""
    if not values:
        return 0
    sum_sq = sum(v * v for v in values)
    return math.sqrt(sum_sq / len(values))


def calc_peak_peak(values):
    """计算峰峰值"""
    if not values:
        return 0
    return max(values) - min(values)


def calc_crest_factor(values):
    """计算波峰因子"""
    if not values:
        return 0
    rms = calc_rms(values)
    peak = max(abs(v) for v in values)
    return peak / rms if rms > 1e-6 else 0


def calc_zero_crossings(values):
    """计算过零次数"""
    if len(values) < 2:
        return 0
    count = 0
    for i in range(1, len(values)):
        if (values[i-1] > 0 and values[i] <= 0) or (values[i-1] < 0 and values[i] >= 0):
            count += 1
    return count


def extract_features(channels):
    """
    从波形数据中提取27维特征向量
    """
    features = []
    
    # 计算各通道RMS
    rms_values = []
    for ch in range(WS_CHANNEL_COUNT):
        rms = calc_rms(channels[ch])
        rms_values.append(rms)
    
    # [0-5] 三相电压/电流RMS
    features.extend(rms_values[:6])
    
    # [6-15] 其他指标（暂用0填充）
    features.extend([0.0] * 10)
    
    # [16-26] 波形特征
    voltage_channels = [channels[0], channels[1], channels[2]]
    pts = len(voltage_channels[0]) if voltage_channels[0] else 0
    
    if pts > 0:
        # 波峰因子
        cf_avg = sum(calc_crest_factor(v) for v in voltage_channels) / 3
        features.append(cf_avg)
        
        # 波形面积
        area_avg = sum(sum(abs(v) for v in ch) for ch in voltage_channels) / 3
        features.append(area_avg)
        
        # 峰峰值
        pp_avg = sum(calc_peak_peak(v) for v in voltage_channels) / 3
        features.append(pp_avg)
        
        # 过零次数
        zc_avg = sum(calc_zero_crossings(v) for v in voltage_channels) / 3
        features.append(zc_avg)
        
        # 斜率均值
        slope_avg = 0
        for v in voltage_channels:
            slopes = [abs(v[i] - v[i-1]) for i in range(1, len(v))]
            slope_avg += sum(slopes) / len(slopes) if slopes else 0
        features.append(slope_avg / 3)
        
        # 标准差
        std_avg = 0
        for v in voltage_channels:
            mean = sum(v) / len(v)
            variance = sum((x - mean) ** 2 for x in v) / len(v)
            std_avg += math.sqrt(variance)
        features.append(std_avg / 3)
        
        # 其余特征
        features.extend([0.0] * 5)
    else:
        features.extend([0.0] * 11)
    
    while len(features) < 27:
        features.append(0.0)
    
    return features[:27]


def ai_inference(features):
    """
    AI推理（简化版）
    """
    ua_rms = features[0]
    ub_rms = features[1]
    uc_rms = features[2]
    
    mean_u = (ua_rms + ub_rms + uc_rms) / 3.0
    variance = sum((v - mean_u) ** 2 for v in [ua_rms, ub_rms, uc_rms]) / 3.0
    std_u = math.sqrt(variance)
    cv = std_u / mean_u if abs(mean_u) > 1e-6 else 0
    
    if_score = min(cv, 1.0)
    ae_score = 0.8 if if_score > 0.5 else 0.1
    
    if if_score > 0.5:
        cnn_class = 3
        cnn_confidence = 0.90
        result_code = 3
        result_desc = b'single phase open'
    elif if_score > 0.2:
        cnn_class = 2
        cnn_confidence = 0.70
        result_code = 2
        result_desc = b'voltage sag'
    elif if_score > 0.1:
        cnn_class = 1
        cnn_confidence = 0.60
        result_code = 1
        result_desc = b'slight anomaly'
    else:
        cnn_class = 0
        cnn_confidence = 0.80
        result_code = 0
        result_desc = b'normal'
    
    return {
        'if_score': if_score,
        'ae_score': ae_score,
        'cnn_class': cnn_class,
        'cnn_confidence': cnn_confidence,
        'result_code': result_code,
        'result_desc': result_desc
    }


def build_response(inference_result, latency_ms):
    """构建AI响应数据包"""
    response = struct.pack(
        AI_RESPONSE_FORMAT,
        PROTO_MAGIC,
        RESP_OK,
        b'\x00\x00\x00',
        inference_result['if_score'],
        inference_result['ae_score'],
        inference_result['cnn_class'],
        inference_result['cnn_confidence'],
        latency_ms,
        inference_result['result_code'],
        inference_result['result_desc']
    )
    return response


def handle_client(client_sock, addr):
    """处理客户端连接"""
    print(f"\n[{datetime.now().strftime('%H:%M:%S')}] 新连接: {addr}")
    
    try:
        # 接收协议头
        header_data = b''
        while len(header_data) < PROTO_HEADER_SIZE:
            chunk = client_sock.recv(PROTO_HEADER_SIZE - len(header_data))
            if not chunk:
                print(f"[{datetime.now().strftime('%H:%M:%S')}] 客户端断开")
                return
            header_data += chunk
        
        # 解析协议头
        magic, version, cmd, reserved, cycle_seq, ts_sec, ts_us, data_len = \
            struct.unpack(PROTO_HEADER_FORMAT, header_data)
        
        print(f"\n{'─'*60}")
        print(f"[{datetime.now().strftime('%H:%M:%S')}] 收到请求:")
        print(f"{'─'*60}")
        print(f"  魔数: 0x{magic:08X}")
        print(f"  版本: {version}")
        print(f"  命令: {cmd} ({'SEND_WAVEFORM' if cmd == CMD_SEND_WAVEFORM else 'UNKNOWN'})")
        print(f"  周波序号: {cycle_seq}")
        print(f"  时间戳: {ts_sec}.{ts_us:06d}")
        print(f"  数据长度: {data_len} 字节")
        
        if magic != PROTO_MAGIC:
            print(f"  ❌ 魔数错误!")
            client_sock.close()
            return
        
        # 接收波形数据
        wave_data = b''
        while len(wave_data) < data_len:
            chunk = client_sock.recv(min(4096, data_len - len(wave_data)))
            if not chunk:
                print(f"  ❌ 接收波形数据中断")
                return
            wave_data += chunk
        
        print(f"\n[STEP 1] 接收波形数据: {len(wave_data)} 字节")
        
        # AI处理
        start_time = time.time()
        
        # 解析波形
        print(f"\n[STEP 2] 解析波形数据...")
        channels, frame_seq, frames_found = parse_waveform(wave_data)
        print(f"  找到 {frames_found} 个有效帧")
        if frame_seq:
            print(f"  周波序号: {frame_seq}")
        
        # 计算RMS
        print(f"\n[STEP 3] 计算通道RMS:")
        for ch in range(WS_CHANNEL_COUNT):
            rms = calc_rms(channels[ch])
            print(f"    {CHANNEL_NAMES[ch]}: {rms:.3f}")
        
        # 特征提取
        print(f"\n[STEP 4] 特征提取...")
        features = extract_features(channels)
        print(f"  特征向量 ({len(features)}维):")
        for i in range(min(6, len(features))):
            print(f"    [{i:2d}] {features[i]:.4f}")
        print(f"    ... ({len(features)-6} more)")
        
        # AI推理
        print(f"\n[STEP 5] AI推理...")
        inference_result = ai_inference(features)
        
        latency_ms = int((time.time() - start_time) * 1000)
        
        print(f"\n[STEP 6] 推理结果:")
        print(f"  iForest得分: {inference_result['if_score']:.4f}")
        print(f"  AE重构误差: {inference_result['ae_score']:.4f}")
        print(f"  CNN分类: {inference_result['cnn_class']}")
        print(f"  置信度: {inference_result['cnn_confidence']:.2f}")
        print(f"  结果码: {inference_result['result_code']}")
        print(f"  描述: {inference_result['result_desc'].decode('ascii', errors='ignore')}")
        print(f"  处理耗时: {latency_ms}ms")
        
        # 发送响应
        response = build_response(inference_result, latency_ms)
        client_sock.sendall(response)
        
        print(f"\n[STEP 7] 已发送响应 ({len(response)} 字节)")
        
        # 分析结论
        print(f"\n{'─'*60}")
        print(f"[结论]")
        if inference_result['result_code'] == 0:
            print(f"  ✅ 波形正常，未检测到异常")
        elif inference_result['result_code'] == 3:
            print(f"  ⚠️ 检测到单相开路!")
            print(f"     符合预期工况: A相加压，B/C相开路")
            print(f"     特征: B/C相RMS异常低，变异系数过高")
        else:
            print(f"  ⚠️ 检测到异常: {inference_result['result_desc'].decode()}")
        print(f"{'─'*60}")
        
    except Exception as e:
        print(f"\n[ERROR] 处理异常: {e}")
        import traceback
        traceback.print_exc()
    finally:
        client_sock.close()


def run_server(host, port):
    """运行AI推理服务端"""
    server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_sock.bind((host, port))
    server_sock.listen(5)
    
    print(f"\n{'='*60}")
    print(f"  RK3576 AI 推理服务端 (模拟版)")
    print(f"  监听地址: {host}:{port}")
    print(f"  等待T536/测试客户端连接...")
    print(f"{'='*60}")
    print()
    
    try:
        while True:
            client_sock, client_addr = server_sock.accept()
            handle_client(client_sock, client_addr)
            
    except KeyboardInterrupt:
        print(f"\n\n[服务器] 正在关闭...")
        server_sock.close()
        print("[服务器] 已停止")


if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(description="RK3576 AI推理服务端(模拟)")
    parser.add_argument("--host", default="127.0.0.1", 
                        help="监听地址 (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=9090, 
                        help="监听端口 (default: 9090)")
    args = parser.parse_args()
    
    run_server(args.host, args.port)