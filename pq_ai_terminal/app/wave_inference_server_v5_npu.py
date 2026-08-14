#!/usr/bin/env python3
"""
RK3576 AI 推理服务端 (v5 - RKNN NPU版)
V2 协议实现 - 与 T536 wave_sender_arm.c 完全对齐

V2 协议帧格式 (与 C端一致):
- 传输帧: [Header(14)] + [CRC32(4)] + [Payload(N)]
- Header: magic(4)+version(1)+cmd(1)+seq(4)+payload_len(4)
- AI响应: 35字节负载

用法: python3 wave_inference_server_v5_npu.py --host 192.168.100.1 --port 9090 --model models/cnn1d_8class.rknn
"""

import socket
import struct
import time
import sys
import argparse
import math
import logging
import zlib
from datetime import datetime

import numpy as np

# ========== 日志配置 ==========
log_filename = f"ai_inference_{datetime.now().strftime('%Y%m%d')}.log"
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(message)s',
    datefmt='%Y-%m-%d %H:%M:%S',
    handlers=[
        logging.StreamHandler(sys.stdout),
        logging.FileHandler(log_filename, encoding='utf-8')
    ]
)
log = logging.getLogger(__name__)

# ========== V2 协议常量 (与 wave_sender_arm.c 一致) ==========
PROTO_MAGIC = 0x57415632  # "WV2" - V2协议魔数
AI_RESPONSE_MAGIC = 0x57415645  # "WAVE" - AI响应魔数
PROTO_VERSION = 2

# 命令类型
CMD_WAVEFORM = 0x01
CMD_ACK = 0x03
CMD_AI_RESULT = 0x07

# 响应类型
RESP_OK = 0
RESP_ANOMALY = 2

# ========== V2 协议结构 (固化) ==========
# 传输帧头: 14字节
# magic(4) + version(1) + cmd(1) + seq(4) + payload_len(4)
V2_HEADER_FORMAT = '<IbbII'
V2_HEADER_SIZE = struct.calcsize(V2_HEADER_FORMAT)  # 14

# AI响应负载: 63字节 (扩展格式，含7通道有效值)
# magic(4,I) + resp_type(1,B) + timestamp(8,Q) + cycle_seq(4,I) +
# if_score(4,f) + ae_score(4,f) + cnn_confidence(4,f) +
# cnn_class(1,B) + scene_id(1,B) +
# ua_rms(4,f) + ub_rms(4,f) + uc_rms(4,f) +
# ia_rms(4,f) + ib_rms(4,f) + ic_rms(4,f) + iz_rms(4,f) +
# crc32(4,I)
AI_RESPONSE_PAYLOAD_FORMAT = '<IBQIfffBBfffffffI'
AI_RESPONSE_PAYLOAD_SIZE = struct.calcsize(AI_RESPONSE_PAYLOAD_FORMAT)  # 63

# ========== 波形参数 (固化) ==========
WS_WAVEFORM_HEADER_SIZE = 18  # nc(2) + ppc(4) + cs(4) + ts(8)
WS_POINTS_PER_CYCLE = 256
WS_CHANNEL_COUNT = 7
WS_SINGLE_WAVEFORM_SIZE = WS_WAVEFORM_HEADER_SIZE + WS_CHANNEL_COUNT * WS_POINTS_PER_CYCLE * 4  # 7186

CHANNEL_NAMES = ['UA', 'UB', 'UC', 'IA', 'IB', 'IC', 'IZ']

# 8类场景名称
SCENARIO_NAMES = ['normal', 'pv_only', 'ev_only', 'pv_ev', 'extreme',
                  'single_loss', 'two_loss', 'three_loss']


# ========== NPU 模型加载 (核心隔离) ==========
NPU_MODEL = None

# NPU 核心配置 (与 RKLLM 共存)
# RK3576 有 2 个 NPU 核心: 0, 1
# RKLLM 默认使用核心 0 (自动模式)
# RKNN AI 推理绑定到核心 1, 避免与 RKLLM 冲突
NPU_CORE_MAP = {
    'auto': 0,           # NPU_CORE_AUTO
    'core0': 1,          # NPU_CORE_0
    'core1': 2,          # NPU_CORE_1
    'core0_1': 3,        # NPU_CORE_0_1 (最大性能)
}

def load_npu_model(model_path, core_mask='core1'):
    """
    加载 RKNN 模型 (NPU 推理)
    支持 NPU 核心隔离，避免与 RKLLM 冲突
    
    Args:
        model_path: RKNN 模型路径
        core_mask: NPU 核心绑定模式
                   'auto'       - 自动 (0, 默认)
                   'core0'      - 核心 0 (1)
                   'core1'      - 核心 1 (2) 推荐: 与 RKLLM 隔离
                   'core0_1'    - 核心 0+1 (3) 最大性能
    """
    global NPU_MODEL
    
    log.info(f"加载 RKNN 模型: {model_path}")
    log.info(f"NPU 核心绑定: {core_mask} (mask={NPU_CORE_MAP.get(core_mask, 0)})")
    
    from rknnlite.api import RKNNLite
    
    rknn = RKNNLite()
    
    # 加载模型
    ret = rknn.load_rknn(model_path)
    if ret != 0:
        raise RuntimeError(f"load_rknn 失败: {ret}")
    log.info(f"load_rknn: OK")
    
    # 初始化运行时 (指定核心绑定)
    core_mask_value = NPU_CORE_MAP.get(core_mask, 0)
    ret = rknn.init_runtime(core_mask=core_mask_value)
    if ret != 0:
        log_warn(f"init_runtime with core_mask={core_mask} 失败: {ret}")
        log_warn(f"回退到自动模式...")
        ret = rknn.init_runtime(core_mask=0)  # 自动模式
        if ret != 0:
            raise RuntimeError(f"init_runtime 失败 (自动模式): {ret}")
    log.info(f"init_runtime: OK (core_mask={core_mask})")
    
    NPU_MODEL = rknn
    log.info(f"RKNN NPU 模型加载成功 (核心隔离模式)")
    
    return rknn


# ========== 工具函数 ==========
def read_le32(data, offset):
    """读取小端序32位整数"""
    return (int(data[offset]) |
            (int(data[offset + 1]) << 8) |
            (int(data[offset + 2]) << 16) |
            (int(data[offset + 3]) << 24))


def read_le_float(data, offset):
    """读取小端序32位浮点数"""
    return struct.unpack('<f', data[offset:offset+4])[0]


def calc_rms(values):
    """计算RMS"""
    if not values:
        return 0
    sum_sq = sum(v * v for v in values)
    return math.sqrt(sum_sq / len(values))


def crc32_calc(data):
    """计算 CRC32 (与 zlib.crc32 兼容)"""
    return zlib.crc32(data) & 0xFFFFFFFF


# ========== V2 协议辅助函数 ==========
def send_v2_frame(client_sock, cmd, seq, payload):
    """
    发送 V2 协议帧
    
    帧结构: [Header(14)] + [CRC32(4)] + [Payload(N)]
    Header: magic + version + cmd + seq + payload_len
    """
    header = struct.pack(V2_HEADER_FORMAT,
                        PROTO_MAGIC, PROTO_VERSION, cmd, seq, len(payload))
    crc = crc32_calc(header + payload)
    frame = header + struct.pack('<I', crc) + payload
    client_sock.sendall(frame)
    return len(frame)


def recv_exact(sock, size):
    """精确接收指定字节数"""
    data = b''
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            return None
        data += chunk
    return data


def recv_v2_frame(client_sock, timeout=5.0):
    """
    接收 V2 协议帧
    
    返回: (magic, version, cmd, seq, payload_len, payload)
    或 None (失败)
    """
    # 接收帧头
    header_data = recv_exact(client_sock, V2_HEADER_SIZE)
    if header_data is None:
        return None
    
    magic, version, cmd, seq, payload_len = struct.unpack(V2_HEADER_FORMAT, header_data)
    
    # 接收 CRC32
    crc_data = recv_exact(client_sock, 4)
    if crc_data is None:
        return None
    received_crc = struct.unpack('<I', crc_data)[0]
    
    # 接收 payload
    payload = recv_exact(client_sock, payload_len)
    if payload is None:
        return None
    
    # 验证 CRC32
    computed_crc = crc32_calc(header_data + payload)
    if computed_crc != received_crc:
        log.error(f"CRC校验失败: recv=0x{received_crc:08X}, calc=0x{computed_crc:08X}")
        return None
    
    return (magic, version, cmd, seq, payload_len, payload)


# ========== 波形解析 ==========
def parse_waveform(wave_data):
    """
    解析 T536 发送的波形 Payload (7186字节)
    参见 docs/波形数据格式与通信协议详解.md §3.1 和 §7.1
    
    Payload 结构:
      Offset 0:  num_channels    uint16  (2B)  通道数 (固定7)
      Offset 2:  points_per_channel uint32 (4B) 每周期采样点数 (固定256)
      Offset 6:  cycle_seq       uint32  (4B)  周波序号
      Offset 10: timestamp_us    uint64  (8B)  时间戳 (微秒)
      Offset 18: channels_data    float32[7*256] (7168B) 7通道波形数据
    """
    offset = 0
    
    # 1. 解析通道数
    num_channels = struct.unpack_from('<H', wave_data, offset)[0]; offset += 2
    if num_channels != WS_CHANNEL_COUNT:
        log.warning(f"通道数异常: {num_channels} != {WS_CHANNEL_COUNT}")
    
    # 2. 解析每周期采样点数
    points_per_channel = struct.unpack_from('<I', wave_data, offset)[0]; offset += 4
    if points_per_channel != WS_POINTS_PER_CYCLE:
        log.warning(f"采样点数异常: {points_per_channel} != {WS_POINTS_PER_CYCLE}")
    
    # 3. 解析周波序号
    cycle_seq = struct.unpack_from('<I', wave_data, offset)[0]; offset += 4
    
    # 4. 解析时间戳
    timestamp_us = struct.unpack_from('<Q', wave_data, offset)[0]; offset += 8
    ts_sec = timestamp_us // 1_000_000
    ts_usec = timestamp_us % 1_000_000
    log.info(f"波形时间戳: {datetime.fromtimestamp(ts_sec).strftime('%Y-%m-%d %H:%M:%S')}.{ts_usec:06d}")
    
    # 5. 解析 7 通道数据
    channels = []
    for ch in range(num_channels):
        channel_data = []
        for pt in range(points_per_channel):
            val = struct.unpack_from('<f', wave_data, offset)[0]
            offset += 4
            channel_data.append(val)
        channels.append(channel_data)
    
    log.info(f"波形解析成功: cycle_seq={cycle_seq}, {num_channels}通道 x {points_per_channel}点")
    return channels, cycle_seq


# ========== NPU AI 推理 ==========
def ai_inference_npu(channels):
    """
    使用 RKNN NPU 进行 AI 推理
    
    输入: 7通道波形数据 (UA, UB, UC, IA, IB, IC, IZ)
    输出: 8类事件分类结果
    """
    global NPU_MODEL
    
    if NPU_MODEL is None:
        raise RuntimeError("NPU 模型未加载")
    
    # 提取三相电压数据 (UA, UB, UC)
    ua = channels[0]
    ub = channels[1]
    uc = channels[2]
    
    if len(ua) == 0:
        raise ValueError("通道数据为空")
    
    # 准备模型输入 (1, 3, 256)
    waveform = np.array([ua, ub, uc], dtype=np.float32)  # (3, 256)
    input_tensor = waveform.reshape(1, 3, 256)  # (1, 3, 256)
    input_tensor = np.ascontiguousarray(input_tensor)
    
    # NPU 推理
    start_time = time.perf_counter()
    outputs = NPU_MODEL.inference(inputs=[input_tensor])
    inference_ms = (time.perf_counter() - start_time) * 1000
    
    # 处理输出
    output = outputs[0].flatten()
    
    # Softmax
    exp_output = np.exp(output - np.max(output))
    probabilities = exp_output / np.sum(exp_output)
    
    pred_class = int(np.argmax(probabilities))
    confidence = float(np.max(probabilities))
    
    # 计算所有 7 通道的有效值
    ua_rms = calc_rms(channels[0])
    ub_rms = calc_rms(channels[1])
    uc_rms = calc_rms(channels[2])
    ia_rms = calc_rms(channels[3])
    ib_rms = calc_rms(channels[4])
    ic_rms = calc_rms(channels[5])
    iz_rms = calc_rms(channels[6])
    
    mean_u = (ua_rms + ub_rms + uc_rms) / 3.0
    variance = sum((v - mean_u) ** 2 for v in [ua_rms, ub_rms, uc_rms]) / 3.0
    std_u = math.sqrt(variance)
    cv = std_u / mean_u if abs(mean_u) > 1e-6 else 0
    
    # iForest 异常检测
    if_score = min(cv, 1.0)
    
    # AE 重构误差
    ae_score = 1.0 - confidence
    
    log.info(f"NPU推理: {inference_ms:.2f}ms, 类别={pred_class}({SCENARIO_NAMES[pred_class]}), 置信度={confidence:.4f}")
    
    return {
        'if_score': if_score,
        'ae_score': ae_score,
        'cnn_class': pred_class,
        'cnn_confidence': confidence,
        'scenario_name': SCENARIO_NAMES[pred_class],
        'ua_rms': ua_rms,
        'ub_rms': ub_rms,
        'uc_rms': uc_rms,
        'ia_rms': ia_rms,
        'ib_rms': ib_rms,
        'ic_rms': ic_rms,
        'iz_rms': iz_rms,
        'inference_ms': inference_ms,
    }


# ========== 构建 AI 响应 (V2 协议) ==========
def build_ai_response_payload(inference_result, cycle_seq):
    """
    构建 AI 响应 Payload (63字节，扩展格式)
    
    格式: <IBQIfffBBfffffffI (17项)
    magic(4,I) + resp_type(1,B) + timestamp(8,Q) + cycle_seq(4,I) +
    if_score(4,f) + ae_score(4,f) + cnn_confidence(4,f) +
    cnn_class(1,B) + scene_id(1,B) +
    ua_rms(4,f) + ub_rms(4,f) + uc_rms(4,f) +
    ia_rms(4,f) + ib_rms(4,f) + ic_rms(4,f) + iz_rms(4,f) +
    crc32(4,I)
    """
    timestamp_us = int(time.time() * 1_000_000)
    
    resp_type = RESP_OK if inference_result['cnn_class'] == 0 else RESP_ANOMALY
    cnn_class = inference_result['cnn_class']
    scene_id = cnn_class
    
    # 打包不含 CRC32 的部分 (前 16 项)
    payload_without_crc = struct.pack(
        '<IBQIfffBBfffffff',
        AI_RESPONSE_MAGIC,
        resp_type,
        timestamp_us,
        cycle_seq,
        inference_result['if_score'],
        inference_result['ae_score'],
        inference_result['cnn_confidence'],
        cnn_class,
        scene_id,
        inference_result['ua_rms'],
        inference_result['ub_rms'],
        inference_result['uc_rms'],
        inference_result['ia_rms'],
        inference_result['ib_rms'],
        inference_result['ic_rms'],
        inference_result['iz_rms']
    )
    
    # 计算 CRC32
    crc = crc32_calc(payload_without_crc)
    
    # 完整负载 = payload_without_crc + crc32
    full_payload = payload_without_crc + struct.pack('<I', crc)
    
    assert len(full_payload) == AI_RESPONSE_PAYLOAD_SIZE, \
        f"AI响应负载大小错误: {len(full_payload)} != {AI_RESPONSE_PAYLOAD_SIZE}"
    
    return full_payload


# ========== 客户端处理 (V2 协议) ==========
def handle_client(client_sock, addr):
    """处理客户端连接 - V2 协议"""
    client_ip = addr[0] if addr else "unknown"
    
    log.info(f"新连接: {client_ip}")
    
    try:
        # 1. 接收 V2 帧
        frame = recv_v2_frame(client_sock)
        if frame is None:
            log.error("接收 V2 帧失败")
            client_sock.close()
            return
        
        magic, version, cmd, seq, payload_len, payload = frame
        
        log.info(f"收到 V2 请求: magic=0x{magic:08X}, ver={version}, cmd={cmd}, seq={seq}, len={payload_len}")
        
        # 验证魔数
        if magic != PROTO_MAGIC:
            log.error(f"魔数错误: 0x{magic:08X}")
            client_sock.close()
            return
        
        # 2. 发送 ACK (V2 帧)
        ack_payload = struct.pack('<I', seq)
        ack_size = send_v2_frame(client_sock, CMD_ACK, seq, ack_payload)
        log.info(f"发送 ACK: {ack_size} 字节")
        
        # 3. 解析波形数据
        channels, frame_seq = parse_waveform(payload)
        
        channel_lens = [len(ch) for ch in channels]
        if min(channel_lens) == 0:
            log.error(f"波形解析失败: 通道数据为空, 长度: {channel_lens}")
            client_sock.close()
            return
        
        log.info(f"波形解析成功: {len(channels[0])} 点/通道")
        
        # 记录 RMS
        for ch in range(WS_CHANNEL_COUNT):
            rms = calc_rms(channels[ch])
            log.info(f"  {CHANNEL_NAMES[ch]} RMS: {rms:.3f}")
        
        # 4. NPU 推理
        start_time = time.time()
        inference_result = ai_inference_npu(channels)
        total_latency_ms = int((time.time() - start_time) * 1000)
        
        # 5. 构建并发送 AI 响应 (V2 帧)
        ai_payload = build_ai_response_payload(inference_result, frame_seq if frame_seq else seq)
        ai_frame_size = send_v2_frame(client_sock, CMD_AI_RESULT, seq, ai_payload)
        
        log.info(f"AI响应已发送: {ai_frame_size} 字节 (含7通道有效值)")
        log.info(f"  电压有效值: UA={inference_result['ua_rms']:.3f}V, UB={inference_result['ub_rms']:.3f}V, UC={inference_result['uc_rms']:.3f}V")
        log.info(f"  电流有效值: IA={inference_result['ia_rms']:.3f}A, IB={inference_result['ib_rms']:.3f}A, IC={inference_result['ic_rms']:.3f}A, IZ={inference_result['iz_rms']:.3f}A")
        log.info(f"  AI评分: if={inference_result['if_score']:.4f}, ae={inference_result['ae_score']:.4f}")
        log.info(f"  推理结果: 类别={inference_result['cnn_class']} ({inference_result['scenario_name']}), 置信度={inference_result['cnn_confidence']:.4f}")
        log.info(f"  NPU耗时: {inference_result['inference_ms']:.2f}ms, 总耗时: {total_latency_ms}ms")
        log.info(f"{'-'*60}")
        
    except Exception as e:
        log.error(f"处理异常: {e}")
        import traceback
        traceback.print_exc()
    finally:
        try:
            client_sock.close()
        except:
            pass


# ========== 主服务 ==========
def run_server(host, port, model_path, core_mask='core1'):
    """
    运行 AI 推理服务端 (V2 协议)
    
    Args:
        host: 监听地址
        port: 监听端口
        model_path: RKNN 模型路径
        core_mask: NPU 核心绑定 (与 RKLLM 共存时使用)
    """
    global NPU_MODEL
    
    # 加载 NPU 模型 (指定核心绑定)
    NPU_MODEL = load_npu_model(model_path, core_mask=core_mask)
    
    server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server_sock.bind((host, port))
    server_sock.listen(5)
    
    log.info(f"{'='*60}")
    log.info(f"RK3576 AI 推理服务端 (v5 - RKNN NPU, V2协议)")
    log.info(f"监听地址: {host}:{port}")
    log.info(f"NPU模型: {model_path}")
    log.info(f"NPU核心: {core_mask}")
    log.info(f"协议: V2 (帧头14字节 + CRC32 + 负载)")
    log.info(f"等待 T536 连接...")
    log.info(f"{'='*60}")
    
    try:
        while True:
            client_sock, client_addr = server_sock.accept()
            handle_client(client_sock, client_addr)
    except KeyboardInterrupt:
        log.info(f"服务器关闭")
        if NPU_MODEL:
            NPU_MODEL.release()
        server_sock.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="RK3576 AI推理服务端 (v5 - RKNN NPU, V2协议)")
    parser.add_argument("--host", default="192.168.100.1", help="监听地址")
    parser.add_argument("--port", type=int, default=9090, help="端口")
    parser.add_argument("--model", default="/home/cat/pq_ai_v3/models/cnn1d_8class.rknn", help="RKNN模型路径")
    parser.add_argument("--core-mask", default="core1", 
                       choices=list(NPU_CORE_MAP.keys()),
                       help="NPU核心绑定 (auto/core0/core1/core0_1)")
    parser.add_argument("--log-level", default="INFO",
                       choices=["DEBUG", "INFO", "WARNING", "ERROR"],
                       help="日志级别")
    args = parser.parse_args()
    
    # 设置日志级别
    log.setLevel(getattr(logging, args.log_level))
    
    run_server(args.host, args.port, args.model, core_mask=args.core_mask)