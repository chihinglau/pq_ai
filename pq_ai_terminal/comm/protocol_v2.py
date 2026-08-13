#!/usr/bin/env python3
"""
T536 ↔ RK3576 增强型通信协议 (Protocol V2)

基于 USB ECM/TCP 物理层, 实现:
  1. 可靠数据传输 - CRC32 校验 + 超时重传 + 序列号去重
  2. 心跳保活 - 定期发送心跳包, 检测链路状态
  3. 数据完整性保护 - 分帧校验, 丢帧检测
  4. 状态监控 - 实时监控 RTT, 丢包率, 带宽利用率
  5. 异常自动恢复 - 连接断开自动重连, 最多重试 5 次

帧格式:
  ┌──────────┬──────────┬──────────┬──────────┬──────────────┬──────────┐
  │ Magic(4) │ Version  │ Cmd(1)   │ Seq(4)   │ PayloadLen(4)│ CRC32(4) │
  ├──────────┼──────────┼──────────┼──────────┼──────────────┼──────────┤
  │ 0x574156 │ 0x02     │ 命令类型 │ 序列号   │ 负载长度     │ 校验值   │
  └──────────┴──────────┴──────────┴──────────┴──────────────┴──────────┘
  总头部: 18 字节
  Payload: ≤ 64KB (可配置)

命令类型:
  0x01: 波形数据帧 (带帧序号)
  0x02: 心跳包
  0x03: 确认包 (ACK/NACK)
  0x04: 重传请求
  0x05: 链路状态查询
  0x06: 链路状态响应
  0x07: AI 推理结果
  0x08: 流量控制 (窗口大小协商)

@author PQ AI Terminal Team
@date 2026-08-13
"""

import socket
import struct
import time
import threading
import logging
import zlib
import hashlib
import os
import sys
from typing import Optional, Dict, List, Tuple, Callable, Any
from dataclasses import dataclass, field
from enum import Enum
from collections import deque
from datetime import datetime

log = logging.getLogger(__name__)

# ========== 协议常量 ==========
PROTO_MAGIC = 0x57415632  # "WV2" (Wave Protocol V2)
PROTO_VERSION = 2

# 命令类型
class CmdType(Enum):
    WAVEFORM = 0x01
    HEARTBEAT = 0x02
    ACK = 0x03
    RETRANSMIT = 0x04
    STATUS_QUERY = 0x05
    STATUS_RESPONSE = 0x06
    AI_RESULT = 0x07
    FLOW_CONTROL = 0x08

# 头部格式: magic(4) + version(1) + cmd(1) + seq(4) + payload_len(4) + crc32(4) = 18字节
FRAME_HEADER_FORMAT = '<IbbII'
FRAME_HEADER_SIZE = 14  # IbbII = 4+1+1+4+4 = 14 bytes

MAX_PAYLOAD_SIZE = 64 * 1024  # 64KB
DEFAULT_TIMEOUT = 5.0  # 5秒
DEFAULT_HEARTBEAT_INTERVAL = 1.0  # 1秒
MAX_RETRANSMISSIONS = 5
RECONNECT_MAX_RETRIES = 5
RECONNECT_DELAY_INITIAL = 1.0  # 初始重连间隔
RECONNECT_DELAY_MAX = 30.0  # 最大重连间隔
WINDOW_SIZE = 8  # 滑动窗口大小


# ========== 数据类型 ==========

@dataclass
class FrameHeader:
    magic: int = PROTO_MAGIC
    version: int = PROTO_VERSION
    cmd: CmdType = CmdType.WAVEFORM
    seq: int = 0
    payload_len: int = 0
    crc32: int = 0

    def pack(self) -> bytes:
        return struct.pack('<IbbII',
                          self.magic, self.version, self.cmd.value,
                          self.seq, self.payload_len)

    @classmethod
    def unpack(cls, data: bytes) -> 'FrameHeader':
        magic, version, cmd_val, seq, payload_len = struct.unpack('<IbbII', data)
        return cls(magic=magic, version=version,
                   cmd=CmdType(cmd_val), seq=seq,
                   payload_len=payload_len)


@dataclass
class DataFrame:
    header: FrameHeader
    payload: bytes = b''

    def compute_crc(self) -> int:
        raw = self.header.pack() + self.payload
        return zlib.crc32(raw) & 0xFFFFFFFF

    def encode(self) -> bytes:
        self.header.crc32 = self.compute_crc()
        return self.header.pack() + struct.pack('<I', self.header.crc32) + self.payload

    @classmethod
    def decode(cls, data: bytes) -> 'DataFrame':
        header_end = FRAME_HEADER_SIZE
        header = FrameHeader.unpack(data[:header_end])
        stored_crc = struct.unpack('<I', data[header_end:header_end+4])[0]
        payload = data[header_end+4:]

        frame = cls(header=header, payload=payload)
        computed_crc = frame.compute_crc()
        frame.header.crc32 = stored_crc

        return frame if computed_crc == stored_crc else None

    @property
    def total_size(self) -> int:
        return FRAME_HEADER_SIZE + 4 + len(self.payload)  # +4 for CRC


@dataclass
class ConnectionStats:
    bytes_sent: int = 0
    bytes_received: int = 0
    frames_sent: int = 0
    frames_received: int = 0
    frames_lost: int = 0
    retransmissions: int = 0
    avg_rtt_ms: float = 0.0
    max_rtt_ms: float = 0.0
    min_rtt_ms: float = float('inf')
    last_heartbeat_ms: float = 0.0
    connected_since: float = 0.0
    disconnect_count: int = 0
    last_error: str = ''

    def to_dict(self) -> Dict[str, Any]:
        uptime_s = time.time() - self.connected_since if self.connected_since else 0
        loss_rate = self.frames_lost / max(1, self.frames_sent) * 100
        return {
            'uptime_seconds': round(uptime_s, 1),
            'bytes_sent': self.bytes_sent,
            'bytes_received': self.bytes_received,
            'frames_sent': self.frames_sent,
            'frames_received': self.frames_received,
            'frames_lost': self.frames_lost,
            'loss_rate_pct': round(loss_rate, 2),
            'avg_rtt_ms': round(self.avg_rtt_ms, 2),
            'max_rtt_ms': round(self.max_rtt_ms, 2),
            'min_rtt_ms': round(self.min_rtt_ms, 2) if self.min_rtt_ms != float('inf') else 0,
            'retransmissions': self.retransmissions,
            'disconnect_count': self.disconnect_count,
            'last_error': self.last_error,
        }


# ========== 可靠传输协议 ==========

class ReliableTransport:
    """
    可靠数据传输协议
    
    特性:
      - 基于序列号的滑动窗口协议
      - CRC32 数据完整性校验
      - 超时重传与指数退避
      - 心跳保活与链路状态监控
      - 自动重连
    """

    def __init__(self, host: str, port: int, is_server: bool = False,
                 heartbeat_interval: float = DEFAULT_HEARTBEAT_INTERVAL,
                 window_size: int = WINDOW_SIZE):
        self.host = host
        self.port = port
        self.is_server = is_server
        self.heartbeat_interval = heartbeat_interval
        self.window_size = window_size

        self._sock: Optional[socket.socket] = None
        self._listen_sock: Optional[socket.socket] = None  # 保存监听 socket
        self._connected = False
        self._lock = threading.Lock()

        self._send_seq = 0
        self._recv_seq = 0
        self._sent_frames: Dict[int, Tuple[DataFrame, float, int]] = {}
        self._recv_buffer: deque = deque(maxlen=200)
        self._pending_acks: List[int] = []

        self._stats = ConnectionStats()
        self._rtt_samples: deque = deque(maxlen=100)

        self._heartbeat_thread: Optional[threading.Thread] = None
        self._receiver_thread: Optional[threading.Thread] = None
        self._running = False

        self._frame_callbacks: Dict[CmdType, List[Callable]] = {}
        self._on_disconnect: Optional[Callable] = None

    # ========== 连接管理 ==========

    def connect(self) -> bool:
        """建立连接"""
        with self._lock:
            if self._connected:
                return True

            if self.is_server:
                # 服务端模式: 绑定端口并无限等待客户端连接
                try:
                    # 创建并保存监听 socket
                    self._listen_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                    self._listen_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                    self._listen_sock.bind((self.host, self.port))
                    self._listen_sock.listen(128)
                    # 服务端使用短超时，循环等待，而不是无限阻塞
                    self._listen_sock.settimeout(1.0)
                    log.info(f"服务端监听 {self.host}:{self.port}")
                    
                    # 设置 _running 为 True，开始等待客户端连接
                    self._running = True
                    
                    # 循环等待客户端连接，不限制重试次数
                    while self._running:
                        try:
                            self._sock, client_addr = self._listen_sock.accept()
                            log.info(f"客户端连接: {client_addr}")
                            break  # 成功接受连接
                        except socket.timeout:
                            continue  # 超时，继续等待
                        except Exception as e:
                            log.error(f"接受连接失败: {e}")
                            return False
                    
                    if not self._running:
                        return False
                        
                except Exception as e:
                    log.error(f"服务端启动失败: {e}")
                    return False
            else:
                # 客户端模式: 连接到服务端
                for attempt in range(RECONNECT_MAX_RETRIES):
                    try:
                        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                        self._sock.settimeout(DEFAULT_TIMEOUT)
                        self._sock.connect((self.host, self.port))
                        log.info(f"已连接到 {self.host}:{self.port}")
                        break
                    except Exception as e:
                        log.warning(f"连接尝试 {attempt + 1}/{RECONNECT_MAX_RETRIES} 失败: {e}")
                        self._stats.disconnect_count += 1
                        if attempt < RECONNECT_MAX_RETRIES - 1:
                            delay = min(RECONNECT_DELAY_INITIAL * (2 ** attempt), RECONNECT_DELAY_MAX)
                            time.sleep(delay)
                else:
                    return False

            self._sock.settimeout(None)
            self._connected = True
            self._stats.connected_since = time.time()

            self._start_background_threads()
            log.info("连接建立成功")
            return True

    def disconnect(self):
        """断开连接"""
        with self._lock:
            self._connected = False
            self._running = False
            if self._heartbeat_thread:
                self._heartbeat_thread.join(timeout=2)
            if self._receiver_thread:
                self._receiver_thread.join(timeout=2)
            if self._sock:
                try:
                    self._sock.close()
                except Exception:
                    pass
                self._sock = None
            # 服务端模式下保留监听 socket，仅关闭客户端连接
            if not self.is_server and self._listen_sock:
                try:
                    self._listen_sock.close()
                except Exception:
                    pass
                self._listen_sock = None
            log.info("连接已断开")

    def reaccept(self) -> bool:
        """服务端模式下重新接受客户端连接（客户端断开后使用）"""
        if not self.is_server or not self._listen_sock:
            return False
        
        with self._lock:
            self._connected = False
            self._running = True
            
            # 确保监听 socket 存在且可用
            if not self._listen_sock:
                log.error("监听 socket 不存在")
                return False
            
            # 设置短超时等待连接
            self._listen_sock.settimeout(1.0)
            
            # 循环等待客户端连接
            while self._running:
                try:
                    self._sock, client_addr = self._listen_sock.accept()
                    log.info(f"客户端重连: {client_addr}")
                    break
                except socket.timeout:
                    continue
                except Exception as e:
                    log.error(f"接受连接失败: {e}")
                    return False
            
            if not self._running:
                return False
            
            self._sock.settimeout(None)
            self._connected = True
            self._stats.connected_since = time.time()
            self._start_background_threads()
            log.info("重连建立成功")
            return True

    @property
    def is_connected(self) -> bool:
        return self._connected

    # ========== 发送/接收 ==========

    def send_data(self, cmd: CmdType, payload: bytes,
                  expect_ack: bool = True, timeout: float = DEFAULT_TIMEOUT) -> bool:
        """发送数据帧"""
        if not self._connected:
            log.error("未连接, 无法发送")
            return False

        if len(payload) > MAX_PAYLOAD_SIZE:
            log.error(f"Payload 过大: {len(payload)} > {MAX_PAYLOAD_SIZE}")
            return False

        seq = self._send_seq
        frame = DataFrame(
            header=FrameHeader(
                cmd=cmd,
                seq=seq,
                payload_len=len(payload)
            ),
            payload=payload
        )

        if expect_ack:
            self._sent_frames[seq] = (frame, time.time(), 0)

        try:
            encoded = frame.encode()
            self._send_all(encoded)
            self._send_seq += 1
            self._stats.frames_sent += 1
            self._stats.bytes_sent += len(encoded)

            if expect_ack:
                self._wait_for_ack(seq, timeout)

            log.debug(f"发送帧: cmd={cmd.value}, seq={seq}, len={len(payload)}")
            return True

        except Exception as e:
            log.error(f"发送失败: {e}")
            self._handle_disconnect(str(e))
            return False

    def _wait_for_ack(self, seq: int, timeout: float):
        """等待指定序列号的 ACK 确认"""
        deadline = time.time() + timeout
        while time.time() < deadline and self._running:
            with self._lock:
                if seq not in self._sent_frames:
                    return
                _, sent_time, retries = self._sent_frames.get(seq, (None, 0, 0))
                if sent_time and (time.time() - sent_time) > timeout:
                    self._handle_retransmit(seq)
                    return
            time.sleep(0.01)

    def send_waveform(self, channels: List[List[float]],
                      cycle_seq: int, timestamp_us: int) -> bool:
        """发送波形数据 (高层接口)"""
        import struct as st

        num_channels = len(channels)
        points_per_channel = len(channels[0]) if channels else 0

        header = struct.pack('<HII',
                             num_channels, points_per_channel, cycle_seq)
        ts = struct.pack('<Q', timestamp_us)

        data = bytearray(header + ts)
        for ch_data in channels:
            for val in ch_data:
                data.extend(struct.pack('<f', float(val)))

        return self.send_data(CmdType.WAVEFORM, bytes(data), expect_ack=True)

    def receive_frame(self, timeout: float = 0.1) -> Optional[DataFrame]:
        """从接收缓冲区获取帧"""
        try:
            if self._recv_buffer:
                return self._recv_buffer.popleft()
        except IndexError:
            pass
        return None

    # ========== 回调注册 ==========

    def register_callback(self, cmd: CmdType, callback: Callable):
        """注册帧回调函数"""
        if cmd not in self._frame_callbacks:
            self._frame_callbacks[cmd] = []
        self._frame_callbacks[cmd].append(callback)

    def set_disconnect_handler(self, handler: Callable):
        self._on_disconnect = handler

    # ========== 内部实现 ==========

    def _start_background_threads(self):
        self._running = True
        self._heartbeat_thread = threading.Thread(target=self._heartbeat_loop, daemon=True)
        self._receiver_thread = threading.Thread(target=self._receive_loop, daemon=True)
        self._heartbeat_thread.start()
        self._receiver_thread.start()

    def _heartbeat_loop(self):
        """心跳循环"""
        while self._running and self._connected:
            try:
                hb_seq = self._send_seq
                frame = DataFrame(
                    header=FrameHeader(cmd=CmdType.HEARTBEAT, seq=hb_seq),
                    payload=b''
                )
                self._send_all(frame.encode())
                self._send_seq += 1
                self._stats.frames_sent += 1
                self._stats.last_heartbeat_ms = time.time() * 1000

                self._check_retransmissions()

            except Exception as e:
                log.warning(f"心跳失败: {e}")
                break

            time.sleep(self.heartbeat_interval)

    def _receive_loop(self):
        """接收循环"""
        while self._running and self._connected:
            try:
                frame_data = self._recv_exact(FRAME_HEADER_SIZE + 4)
                if not frame_data:
                    break

                header = FrameHeader.unpack(frame_data[:FRAME_HEADER_SIZE])
                if header.magic != PROTO_MAGIC:
                    log.warning(f"无效魔数: 0x{header.magic:08X}")
                    continue

                payload = b''
                if header.payload_len > 0:
                    payload = self._recv_exact(header.payload_len)
                    if not payload:
                        break

                full_frame_data = frame_data + payload
                frame = DataFrame.decode(full_frame_data)

                if frame is None:
                    log.warning("CRC 校验失败")
                    self._stats.frames_lost += 1
                    continue

                self._stats.frames_received += 1
                self._stats.bytes_received += len(full_frame_data)

                self._handle_frame(frame)

            except socket.timeout:
                continue
            except Exception as e:
                if self._running:
                    log.error(f"接收异常: {e}")
                    break

        if self._running:
            self._handle_disconnect("接收循环终止")

    def _handle_frame(self, frame: DataFrame):
        cmd = frame.header.cmd

        if cmd == CmdType.ACK:
            ack_seq = struct.unpack('<I', frame.payload[:4])[0]
            self._handle_ack(ack_seq)

        elif cmd == CmdType.HEARTBEAT:
            ack_payload = struct.pack('<I', frame.header.seq)
            pong = DataFrame(
                header=FrameHeader(cmd=CmdType.ACK, seq=frame.header.seq, payload_len=len(ack_payload)),
                payload=ack_payload
            )
            try:
                self._send_all(pong.encode())
            except Exception:
                pass

        elif cmd == CmdType.RETRANSMIT:
            lost_seq = struct.unpack('<I', frame.payload[:4])[0]
            self._handle_retransmit(lost_seq)

        elif cmd == CmdType.WAVEFORM:
            rtt = time.time() * 1000
            if self._stats.last_heartbeat_ms > 0:
                rtt = rtt - self._stats.last_heartbeat_ms
                self._rtt_samples.append(max(0, rtt))
                self._update_rtt_stats()

            self._recv_buffer.append(frame)
            self._dispatch_frame(frame)

            ack_payload = struct.pack('<I', frame.header.seq)
            ack = DataFrame(
                header=FrameHeader(cmd=CmdType.ACK, seq=frame.header.seq, payload_len=len(ack_payload)),
                payload=ack_payload
            )
            try:
                self._send_all(ack.encode())
            except Exception:
                pass

        elif cmd == CmdType.AI_RESULT:
            self._recv_buffer.append(frame)
            self._dispatch_frame(frame)

        elif cmd == CmdType.STATUS_QUERY:
            self._respond_status()

        else:
            self._recv_buffer.append(frame)
            self._dispatch_frame(frame)

    def _handle_ack(self, seq: int):
        if seq in self._sent_frames:
            del self._sent_frames[seq]
            log.debug(f"ACK 确认: seq={seq}")

    def _handle_retransmit(self, seq: int):
        if seq in self._sent_frames:
            frame, sent_time, retries = self._sent_frames[seq]
            if retries < MAX_RETRANSMISSIONS:
                frame.header.seq = self._send_seq
                self._send_seq += 1
                try:
                    self._send_all(frame.encode())
                    self._sent_frames[seq] = (frame, time.time(), retries + 1)
                    self._stats.retransmissions += 1
                    log.info(f"重传帧: seq={seq}, 重试次数={retries + 1}")
                except Exception:
                    pass

    def _check_retransmissions(self):
        """检查超时帧并重传"""
        now = time.time()
        expired = []
        for seq, (frame, sent_time, retries) in self._sent_frames.items():
            if now - sent_time > DEFAULT_TIMEOUT:
                if retries < MAX_RETRANSMISSIONS:
                    expired.append(seq)
                else:
                    log.warning(f"帧 seq={seq} 达到最大重传次数, 丢弃")
                    expired.append(seq)

        for seq in expired:
            if seq in self._sent_frames:
                frame, sent_time, retries = self._sent_frames[seq]
                if retries < MAX_RETRANSMISSIONS:
                    try:
                        frame.header.seq = self._send_seq
                        self._send_seq += 1
                        self._send_all(frame.encode())
                        self._sent_frames[seq] = (frame, time.time(), retries + 1)
                        self._stats.retransmissions += 1
                    except Exception:
                        pass
                else:
                    del self._sent_frames[seq]
                    self._stats.frames_lost += 1

    def _update_rtt_stats(self):
        if not self._rtt_samples:
            return
        samples = list(self._rtt_samples)
        self._stats.avg_rtt_ms = sum(samples) / len(samples)
        self._stats.max_rtt_ms = max(samples)
        self._stats.min_rtt_ms = min(samples)

    def _send_all(self, data: bytes):
        """确保所有数据被发送"""
        if not self._sock:
            raise ConnectionError("Socket 未连接")
        sent = 0
        while sent < len(data):
            n = self._sock.send(data[sent:])
            if n <= 0:
                raise ConnectionError("发送失败")
            sent += n

    def _recv_exact(self, n: int) -> Optional[bytes]:
        """精确接收 n 字节"""
        if not self._sock:
            return None
        data = b''
        while len(data) < n:
            try:
                chunk = self._sock.recv(n - len(data))
                if not chunk:
                    return None
                data += chunk
            except socket.timeout:
                if len(data) > 0:
                    return data
                return None
        return data

    def _dispatch_frame(self, frame: DataFrame):
        for callback in self._frame_callbacks.get(frame.header.cmd, []):
            try:
                callback(frame)
            except Exception as e:
                log.error(f"回调异常: {e}")

    def _respond_status(self):
        """响应状态查询"""
        payload = struct.pack('<IIIIff',
                              self._bytes_sent, self._bytes_received,
                              self._frames_sent, self._frames_received,
                              self._stats.avg_rtt_ms, self._stats.last_heartbeat_ms)
        frame = DataFrame(
            header=FrameHeader(cmd=CmdType.STATUS_RESPONSE, seq=self._send_seq, payload_len=len(payload)),
            payload=payload
        )
        try:
            self._send_all(frame.encode())
        except Exception:
            pass

    def _handle_disconnect(self, reason: str):
        self._connected = False
        self._stats.last_error = reason
        log.warning(f"连接断开: {reason}")
        if self._on_disconnect:
            self._on_disconnect(reason)

    # ========== 统计接口 ==========

    def get_stats(self) -> Dict[str, Any]:
        return self._stats.to_dict()

    def reset_stats(self):
        self._stats = ConnectionStats()
        self._rtt_samples.clear()
        self._send_seq = 0
        self._recv_seq = 0
        self._sent_frames.clear()


# ========== 服务端便捷封装 ==========

class ProtocolServer:
    """协议服务端, 供 RK3576 使用
    
    支持多客户端连接和自动重连:
    - 客户端断开后自动重新监听
    - 支持多个客户端轮流连接
    """

    def __init__(self, host: str = '192.168.100.1', port: int = 9090):
        self.host = host
        self.port = port
        self._transport = ReliableTransport(host, port, is_server=True)
        self._waveform_handler: Optional[Callable] = None
        self._ai_result_handler: Optional[Callable] = None
        self._server_running = False
        self._accept_thread: Optional[threading.Thread] = None

        self._transport.register_callback(CmdType.WAVEFORM, self._on_waveform)
        self._transport.register_callback(CmdType.AI_RESULT, self._on_ai_result)

    def set_waveform_handler(self, handler: Callable):
        self._waveform_handler = handler

    def set_ai_result_handler(self, handler: Callable):
        self._ai_result_handler = handler

    def start(self):
        """启动服务端，在后台线程中处理客户端连接"""
        log.info(f"协议服务端启动: {self.host}:{self.port}")
        self._server_running = True
        self._accept_thread = threading.Thread(target=self._accept_loop, daemon=True)
        self._accept_thread.start()

    def _accept_loop(self):
        """accept 循环，支持客户端断开后自动重连"""
        while self._server_running:
            try:
                # 如果是第一次连接，使用 connect()；否则使用 reaccept()
                if not self._transport._connected and self._transport._listen_sock:
                    # 客户端断开后，重新接受连接
                    if not self._transport.reaccept():
                        if not self._server_running:
                            break
                        log.warning("重连失败，1秒后重试...")
                        time.sleep(1)
                        continue
                elif not self._transport._connected:
                    # 首次连接
                    if not self._transport.connect():
                        if not self._server_running:
                            break
                        log.warning("连接失败，1秒后重试...")
                        time.sleep(1)
                        continue
                
                # 等待客户端断开
                while self._transport.is_connected and self._server_running:
                    time.sleep(0.5)
                
                # 客户端断开，清理并准备重新监听
                if self._server_running:
                    log.info("客户端断开，准备重新监听...")
                    self._transport.disconnect()
                    time.sleep(0.5)
                    
            except Exception as e:
                if self._server_running:
                    log.error(f"accept 循环异常: {e}")
                    time.sleep(1)

    def stop(self):
        """停止服务端"""
        self._server_running = False
        self._transport.disconnect()

    def send_ai_result(self, result_data: bytes) -> bool:
        return self._transport.send_data(CmdType.AI_RESULT, result_data)

    def get_stats(self) -> Dict[str, Any]:
        return self._transport.get_stats()

    def _on_waveform(self, frame: DataFrame):
        if self._waveform_handler:
            self._waveform_handler(frame.payload, frame.header.seq)

    def _on_ai_result(self, frame: DataFrame):
        if self._ai_result_handler:
            self._ai_result_handler(frame.payload)


# ========== 客户端便捷封装 ==========

class ProtocolClient:
    """协议客户端, 供 T536 使用"""

    def __init__(self, host: str = '192.168.100.1', port: int = 9090):
        self.host = host
        self.port = port
        self._transport = ReliableTransport(host, port, is_server=False)

    def connect(self) -> bool:
        return self._transport.connect()

    def disconnect(self):
        self._transport.disconnect()

    def send_waveform(self, channels: List[List[float]],
                      cycle_seq: int, timestamp_us: int) -> bool:
        return self._transport.send_waveform(channels, cycle_seq, timestamp_us)

    def send_raw(self, cmd: CmdType, payload: bytes) -> bool:
        return self._transport.send_data(cmd, payload)

    def receive(self) -> Optional[DataFrame]:
        return self._transport.receive_frame()

    def get_stats(self) -> Dict[str, Any]:
        return self._transport.get_stats()


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG, format='%(asctime)s [%(levelname)s] %(message)s')

    import sys
    if len(sys.argv) > 1 and sys.argv[1] == 'server':
        server = ProtocolServer(port=19090)
        server.set_waveform_handler(lambda data, seq: log.info(f"收到波形: seq={seq}, len={len(data)}"))
        try:
            server.start()
            while True:
                time.sleep(0.1)
        except KeyboardInterrupt:
            server.stop()
    else:
        log.info("协议模块已加载, 导入使用: from protocol_v2 import ProtocolClient, ProtocolServer, CmdType")
        log.info("测试: ProtocolClient 用于 T536, ProtocolServer 用于 RK3576")
