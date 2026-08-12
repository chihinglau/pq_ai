#!/usr/bin/env python3
"""
RK3576 AI 推理服务端 (v3 - 增强日志版)
修复响应格式问题，确保正确返回 48 字节响应

正确的业务流程:
1. T536采集原始波形数据 → 通过USB ECM发送
2. RK3576接收原始波形 → 解析 → 特征提取 → AI推理
3. RK3576返回推理结果给T536

运行: cd ~/ai_inference && PYTHONUNBUFFERED=1 nohup python3 -u wave_inference_server.py --host 192.168.100.1 --port 9090 > ai_server.log 2>&1 &
"""

import socket
import struct
import time
import sys
import argparse
import math
import logging
from datetime import datetime

# ========== 日志配置 ==========
# 同时输出到控制台和文件
log_filename = f"ai_inference_{datetime.now().strftime('%Y%m%d')}.log"

logging.basicConfig(
    level=logging.DEBUG,
    format='%(asctime)s [%(levelname)s] %(message)s',
    datefmt='%Y-%m-%d %H:%M:%S',
    handlers=[
        logging.StreamHandler(sys.stdout),
        logging.FileHandler(log_filename, encoding='utf-8')
    ]
)
log = logging.getLogger(__name__)

# ========== 协议常量 ==========
PROTO_MAGIC = 0x57415645  # "WAVE"
PROTO_VERSION = 1
CMD_SEND_WAVEFORM = 1
RESP_OK = 0
RESP_ERROR = 1

# ========== 协议结构 ==========
# 注意: C端使用memcpy直接发送结构体，ARM为小端序，所以Python端必须用小端序解析
# 传输头格式: magic(4) + version(1) + cmd(1) + reserved(2) + 
#             cycle_seq(4) + ts_sec(4) + ts_us(4) + data_len(4) = 24字节
PROTO_HEADER_FORMAT = '<IbbHIIII'  # 小端序，与C端主机字节序一致
PROTO_HEADER_SIZE = struct.calcsize(PROTO_HEADER_FORMAT)
log.info(f"PROTO_HEADER_SIZE = {PROTO_HEADER_SIZE}")

# AI响应格式: 严格按照 wave_sender_arm.c 中的 ai_response_t 结构
# typedef struct __attribute__((packed)) {
#     uint32_t magic;         /* 4 */
#     uint8_t  resp_type;     /* 1 */
#     uint8_t  reserved[3];   /* 3 */
#     float    if_score;      /* 4 */
#     float    ae_score;      /* 4 */
#     int32_t  cnn_class;     /* 4 */
#     float    cnn_confidence;/* 4 */
#     int32_t  latency_ms;    /* 4 */
#     uint32_t result_code;   /* 4 */
#     char     result_desc[16];/* 16 */
# } ai_response_t;
# 总计: 4+1+3+4+4+4+4+4+4+16 = 48字节

# 注意: C端使用memcpy直接发送/接收结构体，ARM为小端序，所以Python端必须用小端序
# I=4字节无符号, B=1字节无符号, 3s=3字节字符串, f=4字节浮点, i=4字节有符号
# 顺序: magic(I), resp_type(B), reserved(3s), if_score(f), ae_score(f), cnn_class(i), 
#       cnn_confidence(f), latency_ms(i), result_code(I), result_desc(16s)
# C结构体: uint32_t magic; uint8_t resp_type; uint8_t reserved[3]; float if_score;
#          float ae_score; int32_t cnn_class; float cnn_confidence; int32_t latency_ms;
#          uint32_t result_code; char result_desc[16];
AI_RESPONSE_FORMAT = '<IB3sffifIi16s'  # 小端序，48字节，10个参数
AI_RESPONSE_SIZE = struct.calcsize(AI_RESPONSE_FORMAT)
log.info(f"AI_RESPONSE_SIZE = {AI_RESPONSE_SIZE}")

# ========== 波形参数 ==========
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
    """解析T536发送的原始波形数据"""
    channels = [[] for _ in range(WS_CHANNEL_COUNT)]
    frame_seq = None
    idx = 0
    frame_count = 0
    
    log.debug(f"Parsing waveform data: {len(wave_data)} bytes")
    
    while idx < len(wave_data) - 1:
        if wave_data[idx] == 0x68 and wave_data[idx + 1] == 0x36:
            if idx + WS_SINGLE_WAVEFORM_SIZE + 7 > len(wave_data):
                log.warning(f"Incomplete frame at offset {idx}, need {WS_SINGLE_WAVEFORM_SIZE + 7}, have {len(wave_data) - idx}")
                break
            
            frame_len = read_le32(wave_data, idx + 2)
            wave_start = idx + 7
            cycle_seq = read_le32(wave_data, wave_start)
            data_offset = wave_start + 14  # 跳过 cycle_seq(4) + 时间戳(10)
            
            # 解析时间戳
            year = wave_data[wave_start + 4]
            month = wave_data[wave_start + 5]
            day = wave_data[wave_start + 6]
            hour = wave_data[wave_start + 7]
            minute = wave_data[wave_start + 8]
            second = wave_data[wave_start + 9]
            us = read_le32(wave_data, wave_start + 10)
            
            log.debug(f"Frame #{frame_count+1}: offset={idx}, len={frame_len}, seq={cycle_seq}")
            log.debug(f"  Timestamp: {year+2000}-{month:02d}-{day:02d} {hour:02d}:{minute:02d}:{second:02d}.{us//1000:04d}")
            
            # 提取7通道数据
            for ch in range(WS_CHANNEL_COUNT):
                channel_data = []
                for pt in range(WS_POINTS_PER_CYCLE):
                    val = read_le_float(wave_data, data_offset)
                    channel_data.append(val)
                    data_offset += 4
                channels[ch] = channel_data
            
            frame_seq = cycle_seq
            frame_count += 1
            idx += WS_SINGLE_WAVEFORM_SIZE + 7
        else:
            idx += 1
    
    if frame_count == 0:
        log.warning("No valid waveform frames found!")
        log.debug(f"First 32 bytes of data: {wave_data[:32].hex(' ')}")
    else:
        log.info(f"Parsed {frame_count} frame(s), cycle_seq={frame_seq}")
    
    return channels, frame_seq


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
    """从波形数据中提取27维特征向量"""
    features = []
    
    log.info("Extracting 27-dim feature vector...")
    
    # 1-6: 各通道RMS
    rms_values = []
    for ch in range(6):  # UA, UB, UC, IA, IB, IC
        rms = calc_rms(channels[ch])
        rms_values.append(rms)
        features.append(rms)
        log.info(f"  {CHANNEL_NAMES[ch]} RMS: {rms:.4f}")
    
    # 7-16: 占位（直流偏置、谐波等）
    features.extend([0.0] * 10)
    
    # 17-22: 波形特征
    voltage_channels = [channels[0], channels[1], channels[2]]
    
    # 波峰因子
    cf_avg = sum(calc_crest_factor(v) for v in voltage_channels) / 3
    features.append(cf_avg)
    log.info(f"  Crest Factor (avg): {cf_avg:.4f}")
    
    # 波形面积
    area_avg = sum(sum(abs(v) for v in ch) for ch in voltage_channels) / 3
    features.append(area_avg)
    log.info(f"  Waveform Area (avg): {area_avg:.4f}")
    
    # 峰峰值
    pp_avg = sum(calc_peak_peak(v) for v in voltage_channels) / 3
    features.append(pp_avg)
    log.info(f"  Peak-Peak (avg): {pp_avg:.4f}")
    
    # 过零次数
    zc_avg = sum(calc_zero_crossings(v) for v in voltage_channels) / 3
    features.append(zc_avg)
    log.info(f"  Zero Crossings (avg): {zc_avg:.1f}")
    
    # 斜率
    slope_avg = 0
    for v in voltage_channels:
        slopes = [abs(v[i] - v[i-1]) for i in range(1, len(v))]
        slope_avg += sum(slopes) / len(slopes) if slopes else 0
    features.append(slope_avg / 3)
    log.info(f"  Slope (avg): {slope_avg/3:.4f}")
    
    # 标准差
    std_avg = 0
    for v in voltage_channels:
        mean = sum(v) / len(v)
        variance = sum((x - mean) ** 2 for x in v) / len(v)
        std_avg += math.sqrt(variance)
    features.append(std_avg / 3)
    log.info(f"  Std Dev (avg): {std_avg/3:.4f}")
    
    # 23-27: 占位
    features.extend([0.0] * 5)
    
    # 确保27维
    while len(features) < 27:
        features.append(0.0)
    features = features[:27]
    
    log.info(f"Feature vector: {len(features)} dimensions")
    return features


def ai_inference(features):
    """AI推理（简化版）"""
    ua_rms = features[0]
    ub_rms = features[1]
    uc_rms = features[2]
    
    log.info("Running AI inference...")
    log.info(f"Input features: UA_RMS={ua_rms:.3f}, UB_RMS={ub_rms:.3f}, UC_RMS={uc_rms:.3f}")
    
    # 计算变异系数 (CV)
    mean_u = (ua_rms + ub_rms + uc_rms) / 3.0
    variance = sum((v - mean_u) ** 2 for v in [ua_rms, ub_rms, uc_rms]) / 3.0
    std_u = math.sqrt(variance)
    cv = std_u / mean_u if abs(mean_u) > 1e-6 else 0
    
    log.info(f"  Mean voltage: {mean_u:.3f}")
    log.info(f"  Std deviation: {std_u:.3f}")
    log.info(f"  CV (Coefficient of Variation): {cv:.4f}")
    
    # iForest异常检测
    if_score = min(cv, 1.0)
    log.info(f"  iForest score: {if_score:.4f}")
    
    # AE重构误差
    ae_score = 0.8 if if_score > 0.5 else 0.1
    log.info(f"  AE score: {ae_score:.4f}")
    
    # CNN事件分类
    if if_score > 0.5:
        cnn_class = 3
        cnn_confidence = 0.90
        result_code = 3
        result_desc = b'single phase open'
        log.warning(f"  CNN Class: {cnn_class} (Single Phase Open)")
    elif if_score > 0.2:
        cnn_class = 2
        cnn_confidence = 0.70
        result_code = 2
        result_desc = b'voltage sag'
        log.warning(f"  CNN Class: {cnn_class} (Voltage Sag)")
    elif if_score > 0.1:
        cnn_class = 1
        cnn_confidence = 0.60
        result_code = 1
        result_desc = b'slight anomaly'
        log.info(f"  CNN Class: {cnn_class} (Slight Anomaly)")
    else:
        cnn_class = 0
        cnn_confidence = 0.80
        result_code = 0
        result_desc = b'normal'
        log.info(f"  CNN Class: {cnn_class} (Normal)")
    
    log.info(f"  CNN Confidence: {cnn_confidence:.2f}")
    log.info(f"  Result Code: {result_code}")
    log.info(f"  Result Desc: {result_desc.decode('ascii', errors='ignore')}")
    
    return {
        'if_score': if_score,
        'ae_score': ae_score,
        'cnn_class': cnn_class,
        'cnn_confidence': cnn_confidence,
        'result_code': result_code,
        'result_desc': result_desc
    }


def build_response(inference_result, latency_ms):
    """构建AI响应数据包 - 严格按照C端结构"""
    # result_desc 必须是 16 字节，不足补 0
    desc_bytes = inference_result['result_desc']
    if len(desc_bytes) < 16:
        desc_bytes = desc_bytes + b'\x00' * (16 - len(desc_bytes))
    else:
        desc_bytes = desc_bytes[:16]
    
    log.debug("Building AI response packet...")
    log.debug(f"  Magic: 0x{PROTO_MAGIC:08X}")
    log.debug(f"  Response Type: {RESP_OK}")
    log.debug(f"  iForest Score: {inference_result['if_score']:.4f}")
    log.debug(f"  AE Score: {inference_result['ae_score']:.4f}")
    log.debug(f"  CNN Class: {inference_result['cnn_class']}")
    log.debug(f"  CNN Confidence: {inference_result['cnn_confidence']}")
    log.debug(f"  Latency: {latency_ms}ms")
    log.debug(f"  Result Code: {inference_result['result_code']}")
    log.debug(f"  Result Desc: {desc_bytes}")
    
    # 严格按照 AI_RESPONSE_FORMAT 打包
    # 格式: '<IB3sffifIi16s' (10个参数, 48字节)
    response = struct.pack(
        AI_RESPONSE_FORMAT,
        PROTO_MAGIC,                        # I: uint32_t magic (4字节)
        RESP_OK,                            # B: uint8_t resp_type (1字节)
        b'\x00\x00\x00',                   # 3s: uint8_t reserved[3] (3字节)
        inference_result['if_score'],      # f: float if_score (4字节)
        inference_result['ae_score'],      # f: float ae_score (4字节)
        inference_result['cnn_class'],     # i: int32_t cnn_class (4字节)
        inference_result['cnn_confidence'],# f: float cnn_confidence (4字节)
        latency_ms,                        # i: int32_t latency_ms (4字节)
        inference_result['result_code'],   # I: uint32_t result_code (4字节)
        desc_bytes                         # 16s: char result_desc[16] (16字节)
    )
    
    if len(response) != AI_RESPONSE_SIZE:
        log.error(f"Response size mismatch: {len(response)} != {AI_RESPONSE_SIZE}")
        raise ValueError(f"Response size mismatch: {len(response)} != {AI_RESPONSE_SIZE}")
    
    log.debug(f"Response packet size: {len(response)} bytes")
    return response


def handle_client(client_sock, addr):
    """处理客户端连接"""
    client_ip = addr[0] if addr else "unknown"
    client_port = addr[1] if addr else 0
    
    log.info(f"{'='*60}")
    log.info(f"New connection from {client_ip}:{client_port}")
    log.info(f"{'='*60}")
    
    try:
        # 接收协议头
        header_data = b''
        while len(header_data) < PROTO_HEADER_SIZE:
            chunk = client_sock.recv(PROTO_HEADER_SIZE - len(header_data))
            if not chunk:
                log.warning(f"Client {client_ip} disconnected during header receive")
                return
            header_data += chunk
        
        # 解析协议头
        magic, version, cmd, reserved, cycle_seq, ts_sec, ts_us, data_len = \
            struct.unpack(PROTO_HEADER_FORMAT, header_data)
        
        log.info(f"Received request:")
        log.info(f"  Magic: 0x{magic:08X}")
        log.info(f"  Version: {version}")
        log.info(f"  Command: {cmd} ({'SEND_WAVEFORM' if cmd == CMD_SEND_WAVEFORM else 'UNKNOWN'})")
        log.info(f"  Cycle Seq: {cycle_seq}")
        log.info(f"  Timestamp: {ts_sec}.{ts_us:06d}")
        log.info(f"  Data Length: {data_len} bytes")
        
        # 验证魔数
        if magic != PROTO_MAGIC:
            log.error(f"Invalid magic: 0x{magic:08X} != 0x{PROTO_MAGIC:08X}")
            log.error(f"Rejecting request from {client_ip}")
            client_sock.close()
            return
        
        if cmd != CMD_SEND_WAVEFORM:
            log.error(f"Unknown command: {cmd}")
            client_sock.close()
            return
        
        # 验证数据长度合理性
        if data_len == 0 or data_len > 1000000:  # 最多1MB
            log.error(f"Invalid data length: {data_len}")
            client_sock.close()
            return
        
        # 接收波形数据
        wave_data = b''
        received = 0
        while received < data_len:
            chunk_size = min(4096, data_len - received)
            chunk = client_sock.recv(chunk_size)
            if not chunk:
                log.error(f"Connection interrupted at {received}/{data_len} bytes")
                return
            wave_data += chunk
            received += len(chunk)
        
        log.info(f"Waveform data received: {len(wave_data)} bytes")
        
        # ========== AI处理 ==========
        start_time = time.time()
        
        # Step 1: 解析波形
        log.info(f"--- AI Processing Start ---")
        log.info(f"Step 1: Parsing waveform...")
        channels, frame_seq = parse_waveform(wave_data)
        
        if frame_seq is not None:
            log.info(f"  Waveform parsed successfully, frame_seq={frame_seq}")
        else:
            log.warning(f"  No valid waveform frames found")
        
        # Step 2: 计算RMS
        log.info(f"Step 2: Calculating RMS for each channel...")
        rms_values = []
        for ch in range(WS_CHANNEL_COUNT):
            rms = calc_rms(channels[ch])
            rms_values.append(rms)
            log.info(f"  {CHANNEL_NAMES[ch]}: RMS = {rms:.3f}")
        
        # Step 3: 特征提取
        log.info(f"Step 3: Extracting features (27-dim)...")
        features = extract_features(channels)
        
        # Step 4: AI推理
        log.info(f"Step 4: Running AI inference...")
        inference_result = ai_inference(features)
        
        latency_ms = int((time.time() - start_time) * 1000)
        
        log.info(f"--- AI Processing Complete ({latency_ms}ms) ---")
        log.info(f"Results:")
        log.info(f"  iForest Score: {inference_result['if_score']:.4f}")
        log.info(f"  AE Score: {inference_result['ae_score']:.4f}")
        log.info(f"  CNN Class: {inference_result['cnn_class']}")
        log.info(f"  CNN Confidence: {inference_result['cnn_confidence']:.2f}")
        log.info(f"  Result Code: {inference_result['result_code']}")
        log.info(f"  Result Desc: {inference_result['result_desc'].decode('ascii', errors='ignore')}")
        
        if inference_result['result_code'] > 0:
            log.warning(f"  ⚠️ ANOMALY DETECTED! Code={inference_result['result_code']}")
        else:
            log.info(f"  ✅ Normal operation")
        
        # Step 5: 构建并发送响应
        log.info(f"Step 5: Building and sending response...")
        response = build_response(inference_result, latency_ms)
        
        # 使用 sendall 确保全部发送
        client_sock.sendall(response)
        log.info(f"  Response sent: {len(response)} bytes")
        
        log.info(f"{'='*60}")
        
    except Exception as e:
        log.error(f"Error processing request from {client_ip}: {e}")
        import traceback
        traceback.print_exc()
    finally:
        try:
            client_sock.close()
        except:
            pass
        log.info(f"Connection from {client_ip} closed")


def run_server(host, port):
    """运行AI推理服务端"""
    server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_sock.bind((host, port))
    server_sock.listen(5)
    
    log.info(f"{'='*60}")
    log.info(f"RK3576 AI Inference Server (v3 Enhanced)")
    log.info(f"Listening on {host}:{port}")
    log.info(f"Waiting for T536 connections...")
    log.info(f"Protocol: <IbbHIIII (24-byte header), <IB3sffifIi16s (48-byte response)")
    log.info(f"Log file: {log_filename}")
    log.info(f"{'='*60}")
    log.info(f"")
    
    try:
        while True:
            client_sock, client_addr = server_sock.accept()
            log.info(f"Accepted connection from {client_addr}")
            handle_client(client_sock, client_addr)
            
    except KeyboardInterrupt:
        log.info(f"\n\nServer shutting down...")
        server_sock.close()
        log.info(f"Server stopped")
    except Exception as e:
        log.error(f"Server error: {e}")
        server_sock.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="RK3576 AI推理服务端 (增强日志版)")
    parser.add_argument("--host", default="192.168.100.1", 
                        help="监听地址 (RK3576 usb0, default: 192.168.100.1)")
    parser.add_argument("--port", type=int, default=9090, 
                        help="监听端口 (default: 9090)")
    parser.add_argument("--log-level", default="INFO",
                        choices=["DEBUG", "INFO", "WARNING", "ERROR"],
                        help="日志级别 (default: INFO)")
    args = parser.parse_args()
    
    # 设置日志级别
    log_level = getattr(logging, args.log_level)
    log.setLevel(log_level)
    
    log.info(f"Starting RK3576 AI Inference Server...")
    log.info(f"  Host: {args.host}")
    log.info(f"  Port: {args.port}")
    log.info(f"  Log Level: {args.log_level}")
    log.info(f"  Log File: {log_filename}")
    
    run_server(args.host, args.port)
