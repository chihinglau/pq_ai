#!/usr/bin/env python3
"""
实时波形处理流水线 (Waveform Processing Pipeline)

基于 RK3576 的实时波形处理架构, 实现:
  1. 数据接收线程 - 从通信协议接收波形数据
  2. 预处理线程 - 波形解析、校验、格式化
  3. 特征提取线程 - 27维特征向量提取
  4. AI 推理线程 - NPU/CPU 推理执行
  5. 结果分发线程 - 结果缓存、存储、上报

设计要点:
  - 多生产者-多消费者队列模型
  - 各阶段并行执行, 流水线处理
  - 推理延迟 < 采样周期的 50% (< 10ms)
  - 结果环形缓存, 支持历史查询与趋势分析

流水线:
  接收 → 预处理 → 特征提取 → AI推理 → 结果分发
  (Q1)    (Q2)      (Q3)       (Q4)      (缓存/存储)

@author PQ AI Terminal Team
@date 2026-08-13
"""

import os
import sys
import time
import math
import logging
import queue
import threading
import json
import struct
import numpy as np
from typing import Optional, Dict, List, Tuple, Any, Callable
from dataclasses import dataclass, field
from enum import Enum
from collections import deque, OrderedDict
from datetime import datetime

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

log = logging.getLogger(__name__)

# ========== 配置 ==========
SAMPLE_PERIOD_MS = 20  # 50Hz 工频 = 20ms
MAX_INFERENCE_DELAY_MS = SAMPLE_PERIOD_MS * 0.5  # 10ms
QUEUE_MAX_SIZE = 512  # 各阶段队列最大长度
RESULT_CACHE_SIZE = 1000  # 结果缓存条数
FEATURE_CACHE_SIZE = 500  # 特征缓存条数

# 推理模式枚举
class InferenceMode(Enum):
    """推理模式: heuristic=物理启发式(稳定可靠), npu=NPU AI推理(需模型训练)"""
    HEURISTIC = 'heuristic'
    NPU = 'npu'

# CNN 分类标签映射
_CNN_CLASS_MAP = {0: "正常", 1: "电压暂降", 2: "电压暂升", 3: "谐波",
    4: "三相不平衡", 5: "过载", 6: "瞬态脉冲"}

# ========== 数据类型 ==========

@dataclass
class WaveformPacket:
    """原始波形数据包"""
    cycle_seq: int
    timestamp_us: int
    channels: List[List[float]]
    receive_time: float = 0.0
    preprocess_time: float = 0.0
    feature_time: float = 0.0
    inference_time: float = 0.0
    total_latency_ms: float = 0.0

    @property
    def channel_count(self) -> int:
        return len(self.channels)

    @property
    def points_per_channel(self) -> int:
        return len(self.channels[0]) if self.channels else 0


@dataclass
class FeatureVector:
    """27维特征向量"""
    values: List[float]
    cycle_seq: int
    timestamp_us: int

    def to_dict(self) -> Dict[str, Any]:
        feature_names = [
            'ua_rms', 'ub_rms', 'uc_rms', 'ia_rms', 'ib_rms', 'ic_rms',
            'ua_thd', 'ub_thd', 'uc_thd', 'ia_thd', 'ib_thd', 'ic_thd',
            'unbalance_pct', 'crest_factor_avg', 'waveform_area_avg',
            'peak_peak_avg', 'zero_crossings_avg', 'slope_avg', 'std_avg',
            'iforest_input_1', 'iforest_input_2', 'iforest_input_3',
            'harmonic_3', 'harmonic_5', 'harmonic_7', 'harmonic_9', 'harmonic_11'
        ]
        return {name: round(val, 6) for name, val in zip(feature_names[:len(self.values)], self.values)}


@dataclass
class InferenceResult:
    """AI 推理结果"""
    cycle_seq: int
    timestamp_us: int
    if_score: float = 0.0
    ae_score: float = 0.0
    cnn_class: int = 0
    cnn_confidence: float = 0.0
    latency_ms: float = 0.0
    backend: str = 'cpu'
    is_anomaly: bool = False
    scene_id: str = 'S1'

    def to_dict(self) -> Dict[str, Any]:
        return {
            'cycle_seq': self.cycle_seq,
            'timestamp_us': self.timestamp_us,
            'if_score': round(self.if_score, 4),
            'ae_score': round(self.ae_score, 4),
            'cnn_class': self.cnn_class,
            'cnn_confidence': round(self.cnn_confidence, 4),
            'latency_ms': round(self.latency_ms, 3),
            'backend': self.backend,
            'is_anomaly': self.is_anomaly,
            'scene_id': self.scene_id,
            'datetime': datetime.fromtimestamp(self.timestamp_us / 1_000_000).isoformat()
                        if self.timestamp_us > 0 else '',
        }


@dataclass
class PipelineMetrics:
    """流水线性能指标"""
    total_packets: int = 0
    total_anomalies: int = 0
    avg_throughput: float = 0.0  # packets/s
    max_queue_depth: int = 0
    avg_preprocess_ms: float = 0.0
    avg_feature_ms: float = 0.0
    avg_inference_ms: float = 0.0
    avg_total_latency_ms: float = 0.0
    latency_target_met_pct: float = 100.0
    dropped_packets: int = 0
    uptime_seconds: float = 0.0

    def to_dict(self) -> Dict[str, Any]:
        return {
            'total_packets': self.total_packets,
            'total_anomalies': self.total_anomalies,
            'avg_throughput_pps': round(self.avg_throughput, 1),
            'max_queue_depth': self.max_queue_depth,
            'avg_preprocess_ms': round(self.avg_preprocess_ms, 3),
            'avg_feature_ms': round(self.avg_feature_ms, 3),
            'avg_inference_ms': round(self.avg_inference_ms, 3),
            'avg_total_latency_ms': round(self.avg_total_latency_ms, 3),
            'latency_target_met_pct': round(self.latency_target_met_pct, 1),
            'dropped_packets': self.dropped_packets,
            'uptime_seconds': round(self.uptime_seconds, 1),
        }


# ========== 实时波形处理流水线 ==========

class WaveformPipeline:
    """
    实时波形处理流水线
    
    使用多级队列 + 多线程实现流水线并行处理。
    每个阶段独立运行, 不阻塞其他阶段。
    
    处理流程:
      1. 接收 → 预处理 (Q1 → Q2)
      2. 预处理 → 特征提取 (Q2 → Q3)
      3. 特征提取 → AI推理 (Q3 → Q4)
      4. AI推理 → 结果缓存/存储
    """

    def __init__(self, npu_engine=None, llm_assistant=None, 
                 inference_mode=InferenceMode.HEURISTIC):
        self._npu_engine = npu_engine
        self._llm_assistant = llm_assistant
        self._inference_mode = inference_mode  # 推理模式: heuristic 或 npu
        self._npu_fallback_count = 0  # NPU 回退计数器
        
        log.info(f"WaveformPipeline 初始化: 推理模式={self._inference_mode.value}")

        self._q1 = queue.Queue(maxsize=QUEUE_MAX_SIZE)  # 原始波形
        self._q2 = queue.Queue(maxsize=QUEUE_MAX_SIZE)  # 预处理后
        self._q3 = queue.Queue(maxsize=QUEUE_MAX_SIZE)  # 特征向量
        self._q4 = queue.Queue(maxsize=QUEUE_MAX_SIZE)  # 推理结果

        self._result_cache: deque = deque(maxlen=RESULT_CACHE_SIZE)
        self._feature_cache: deque = deque(maxlen=FEATURE_CACHE_SIZE)
        self._anomaly_cache: deque = deque(maxlen=200)

        self._workers: List[threading.Thread] = []
        self._running = False
        self._start_time = 0.0

        self._total_packets = 0
        self._total_anomalies = 0
        self._dropped_packets = 0
        self._latency_samples: deque = deque(maxlen=1000)
        self._preprocess_samples: deque = deque(maxlen=500)
        self._feature_samples: deque = deque(maxlen=500)
        self._inference_samples: deque = deque(maxlen=500)

        self._queue_max_depth = 0

        self._result_callbacks: List[Callable[[InferenceResult], None]] = []
        self._anomaly_callbacks: List[Callable[[InferenceResult], None]] = []

        self._lock = threading.Lock()

    # ========== 启动/停止 ==========

    def start(self, num_workers: int = 3):
        """启动流水线"""
        if self._running:
            return

        self._running = True
        self._start_time = time.time()

        self._workers.append(
            threading.Thread(target=self._preprocess_worker, name='preprocess', daemon=True))
        self._workers.append(
            threading.Thread(target=self._feature_worker, name='feature', daemon=True))
        for i in range(num_workers):
            self._workers.append(
                threading.Thread(target=self._inference_worker, name=f'inference-{i}', daemon=True))
        self._workers.append(
            threading.Thread(target=self._dispatch_worker, name='dispatch', daemon=True))

        for w in self._workers:
            w.start()

        log.info(f"波形处理流水线已启动: {len(self._workers)} 工作线程")

    def stop(self):
        """停止流水线"""
        self._running = False
        for w in self._workers:
            w.join(timeout=3)
        self._workers.clear()
        log.info("波形处理流水线已停止")

    # ========== 数据输入 ==========

    def submit_waveform(self, packet: WaveformPacket) -> bool:
        """提交波形数据到流水线"""
        if not self._running:
            log.debug("[数据接收] 流水线未运行, 拒绝 seq=%d", packet.cycle_seq)
            return False

        packet.receive_time = time.time()
        try:
            q_depth = self._q1.qsize()
            self._q1.put_nowait(packet)
            self._update_queue_depth()
            log.debug("[数据接收] seq=%d, 通道=%d, 采样点=%d, 队列深度=%d, 耗时=%.3fms",
                      packet.cycle_seq, packet.channel_count,
                      packet.points_per_channel, q_depth,
                      (time.time() - packet.receive_time) * 1000)
            return True
        except queue.Full:
            self._dropped_packets += 1
            log.warning("[数据接收] Q1 队列已满(%d/%d), 丢弃 seq=%d",
                      self._q1.qsize(), QUEUE_MAX_SIZE, packet.cycle_seq)
            return False

    # ========== 回调注册 ==========

    def add_result_callback(self, callback: Callable[[InferenceResult], None]):
        self._result_callbacks.append(callback)

    def add_anomaly_callback(self, callback: Callable[[InferenceResult], None]):
        self._anomaly_callbacks.append(callback)

    # ========== 工作线程实现 ==========

    def _preprocess_worker(self):
        """预处理: 波形解析、校验、标准化"""
        log.info("[预处理] 工作线程启动")
        while self._running:
            try:
                packet = self._q1.get(timeout=0.5)
            except queue.Empty:
                continue

            try:
                t0 = time.time()
                self._preprocess(packet)
                packet.preprocess_time = time.time() - t0
                self._preprocess_samples.append(packet.preprocess_time * 1000)

                q_depth = self._q2.qsize()
                self._q2.put_nowait(packet)
                self._update_queue_depth()
                
                total_so_far = (time.time() - packet.receive_time) * 1000
                log.debug("[预处理] seq=%d, 耗时=%.3fms, 预处理=%.3fms, Q2=%d, 累计=%.3fms",
                          packet.cycle_seq, packet.preprocess_time * 1000,
                          packet.preprocess_time * 1000, q_depth, total_so_far)

            except Exception as e:
                log.error("[预处理] seq=%d 异常: %s, 通道=%d, 数据点=%d",
                          packet.cycle_seq, e, packet.channel_count, packet.points_per_channel)
            finally:
                self._q1.task_done()

    def _feature_worker(self):
        """特征提取"""
        log.info("[特征提取] 工作线程启动")
        while self._running:
            try:
                packet = self._q2.get(timeout=0.5)
            except queue.Empty:
                continue

            try:
                t0 = time.time()
                features = self._extract_features(packet)
                packet.feature_time = time.time() - t0
                self._feature_samples.append(packet.feature_time * 1000)

                q_depth = self._q3.qsize()
                self._q3.put_nowait((packet, features))
                self._update_queue_depth()
                
                total_so_far = (time.time() - packet.receive_time) * 1000
                ua = features.values[0] if len(features.values) > 0 else 0
                ub = features.values[1] if len(features.values) > 1 else 0
                uc = features.values[2] if len(features.values) > 2 else 0
                log.debug("[特征提取] seq=%d, 耗时=%.3fms, 特征维度=%d, Ua=%.2f, Ub=%.2f, Uc=%.2f, Q3=%d, 累计=%.3fms",
                          packet.cycle_seq, packet.feature_time * 1000,
                          len(features.values), ua, ub, uc, q_depth, total_so_far)

            except Exception as e:
                log.error("[特征提取] seq=%d 异常: %s", packet.cycle_seq, e)
            finally:
                self._q2.task_done()

    def _inference_worker(self):
        """AI 推理 (支持 NPU 加速)"""
        log.info("[AI推理] 工作线程启动")
        while self._running:
            try:
                packet, features = self._q3.get(timeout=0.5)
            except queue.Empty:
                continue

            try:
                t0 = time.time()
                result = self._run_inference(packet, features)
                packet.inference_time = time.time() - t0
                self._inference_samples.append(packet.inference_time * 1000)

                q_depth = self._q4.qsize()
                self._q4.put_nowait((packet, features, result))
                self._update_queue_depth()
                
                total_so_far = (time.time() - packet.receive_time) * 1000
                log.debug("[AI推理] seq=%d, 推理耗时=%.3fms, 后端=%s, IF=%.4f, AE=%.4f, CNN=%d(%s), 置信度=%.4f, Q4=%d, 累计=%.3fms",
                          packet.cycle_seq, packet.inference_time * 1000,
                          result.backend, result.if_score, result.ae_score,
                          result.cnn_class, _CNN_CLASS_MAP.get(result.cnn_class, "?"),
                          result.cnn_confidence, q_depth, total_so_far)

            except Exception as e:
                log.error("[AI推理] seq=%d 异常: %s, 特征维度=%d",
                          packet.cycle_seq, e, len(features.values))
            finally:
                self._q3.task_done()

    def _dispatch_worker(self):
        """结果分发: 缓存、存储、回调"""
        log.info("[结果分发] 工作线程启动")
        while self._running:
            try:
                packet, features, result = self._q4.get(timeout=0.5)
            except queue.Empty:
                continue

            try:
                result.total_latency_ms = (time.time() - packet.receive_time) * 1000
                self._latency_samples.append(result.total_latency_ms)

                self._total_packets += 1
                if result.is_anomaly:
                    self._total_anomalies += 1
                    self._anomaly_cache.append(result)
                    log.warning("[结果分发] seq=%d 检测到异常: IF=%.4f, CNN=%d(%s), 总延迟=%.2fms",
                              result.cycle_seq, result.if_score, result.cnn_class,
                              _CNN_CLASS_MAP.get(result.cnn_class, "?"), result.total_latency_ms)

                self._result_cache.append(result)
                self._feature_cache.append(features)

                for cb in self._result_callbacks:
                    try:
                        cb(result)
                    except Exception as e:
                        log.error("[结果分发] 结果回调异常: seq=%d, 错误=%s", result.cycle_seq, e)

                if result.is_anomaly:
                    for cb in self._anomaly_callbacks:
                        try:
                            cb(result)
                        except Exception as e:
                            log.error("[结果分发] 异常回调异常: seq=%d, 错误=%s", result.cycle_seq, e)

                if self._total_packets % 100 == 0:
                    log.info("[结果分发] 已处理=%d, 异常=%d, 丢弃=%d, 吞吐=%.1fpps, 平均延迟=%.3fms",
                            self._total_packets, self._total_anomalies, self._dropped_packets,
                            self._total_packets / max(0.001, time.time() - self._start_time) * 1000,
                            sum(self._latency_samples) / max(1, len(self._latency_samples)))

            except Exception as e:
                log.error("[结果分发] seq=%d 分发异常: %s", packet.cycle_seq, e)
            finally:
                self._q4.task_done()

    # ========== 内部处理方法 ==========

    def _preprocess(self, packet: WaveformPacket):
        """波形预处理"""
        if not packet.channels or not packet.channels[0]:
            raise ValueError("空波形数据")

        for ch_idx, channel in enumerate(packet.channels):
            if len(channel) < 100:
                log.warning(f"通道 {ch_idx} 数据过短: {len(channel)} 点")

        return True

    def _extract_features(self, packet: WaveformPacket) -> FeatureVector:
        """提取 27 维特征向量"""
        channels = packet.channels
        features = []

        # 1-6: 各通道 RMS
        for ch_idx in range(min(6, len(channels))):
            rms = self._calc_rms(channels[ch_idx])
            features.append(rms)

        while len(features) < 6:
            features.append(0.0)

        # 7-12: 各通道 THD (简化计算)
        for ch_idx in range(min(6, len(channels))):
            thd = self._calc_thd_simple(channels[ch_idx])
            features.append(thd)

        # 13: 三相不平衡度
        ua, ub, uc = features[0], features[1], features[2]
        voltages = [v for v in [ua, ub, uc] if v > 0.1]
        if len(voltages) >= 2:
            avg_v = sum(voltages) / len(voltages)
            max_dev = max(abs(v - avg_v) for v in voltages)
            unbalance = max_dev / avg_v * 100 if avg_v > 0 else 0
        else:
            unbalance = 0
        features.append(unbalance)

        # 14: 平均波峰因子
        crest_factors = []
        for ch_idx in range(3):
            if ch_idx < len(channels) and len(channels[ch_idx]) > 0:
                rms = self._calc_rms(channels[ch_idx])
                peak = max(abs(v) for v in channels[ch_idx])
                if rms > 1e-6:
                    crest_factors.append(peak / rms)
        features.append(sum(crest_factors) / len(crest_factors) if crest_factors else 0)

        # 15: 平均波形面积
        areas = []
        for ch_idx in range(3):
            if ch_idx < len(channels):
                areas.append(sum(abs(v) for v in channels[ch_idx]))
        features.append(sum(areas) / len(areas) if areas else 0)

        # 16: 平均峰峰值
        pps = []
        for ch_idx in range(3):
            if ch_idx < len(channels):
                pps.append(max(channels[ch_idx]) - min(channels[ch_idx]))
        features.append(sum(pps) / len(pps) if pps else 0)

        # 17: 过零次数
        zcs = []
        for ch_idx in range(3):
            if ch_idx < len(channels) and len(channels[ch_idx]) >= 2:
                count = 0
                data = channels[ch_idx]
                for i in range(1, len(data)):
                    if (data[i-1] > 0 and data[i] <= 0) or (data[i-1] < 0 and data[i] >= 0):
                        count += 1
                zcs.append(count)
        features.append(sum(zcs) / len(zcs) if zcs else 0)

        # 18: 平均斜率
        slopes = []
        for ch_idx in range(3):
            if ch_idx < len(channels) and len(channels[ch_idx]) >= 2:
                s = 0
                data = channels[ch_idx]
                for i in range(1, len(data)):
                    s += abs(data[i] - data[i-1])
                slopes.append(s / len(data))
        features.append(sum(slopes) / len(slopes) if slopes else 0)

        # 19: 标准差
        stds = []
        for ch_idx in range(3):
            if ch_idx < len(channels) and len(channels[ch_idx]) > 0:
                data = channels[ch_idx]
                mean = sum(data) / len(data)
                var = sum((x - mean) ** 2 for x in data) / len(data)
                stds.append(math.sqrt(var))
        features.append(sum(stds) / len(stds) if stds else 0)

        # 20-27: 高频特征 (简化占位)
        features.extend([0.0] * 8)

        while len(features) < 27:
            features.append(0.0)
        features = features[:27]

        return FeatureVector(
            values=features,
            cycle_seq=packet.cycle_seq,
            timestamp_us=packet.timestamp_us
        )

    def _run_inference(self, packet: WaveformPacket,
                       features: FeatureVector) -> InferenceResult:
        """运行 AI 推理，支持 heuristic 和 npu 两种模式，带智能回退"""
        t0 = time.time()

        # === 模式1: 物理启发式推理 (稳定可靠, 使用基于特征的确定性计算) ===
        if self._inference_mode == InferenceMode.HEURISTIC:
            if_score, ae_score, cnn_class, cnn_confidence = self._cpu_inference_fallback(
                features, packet)
            backend = 'heuristic'

        # === 模式2: NPU AI 推理 (实验性, 需模型训练) ===
        elif self._inference_mode == InferenceMode.NPU:
            if_score, ae_score, cnn_class, cnn_confidence, backend = self._run_npu_inference_safe(
                packet, features)
        else:
            # 未知模式，安全回退
            log.warning(f"未知推理模式: {self._inference_mode}, 使用 heuristic")
            if_score, ae_score, cnn_class, cnn_confidence = self._cpu_inference_fallback(
                features, packet)
            backend = 'heuristic'

        latency_ms = (time.time() - t0) * 1000
        # 改进的异常判定：降低阈值，支持多种异常类型检测
        # IF > 0.4 或 CNN 分类为异常 (class 1/2/3) 或 AE 分数异常
        is_anomaly = if_score > 0.4 or cnn_class > 0 or ae_score > 100.0

        scene_id = self._determine_scene(if_score, cnn_class, features.values)

        return InferenceResult(
            cycle_seq=packet.cycle_seq,
            timestamp_us=packet.timestamp_us,
            if_score=if_score,
            ae_score=ae_score,
            cnn_class=cnn_class,
            cnn_confidence=cnn_confidence,
            latency_ms=latency_ms,
            backend=backend,
            is_anomaly=is_anomaly,
            scene_id=scene_id,
        )

    def _run_npu_inference_safe(self, packet: WaveformPacket,
                                 features: FeatureVector) -> Tuple[float, float, int, float, str]:
        """安全的 NPU 推理，带结果有效性验证和自动回退"""
        
        # 如果 NPU 引擎不可用，直接回退
        if not self._npu_engine or self._npu_engine.get_backend().value == 'cpu':
            log.debug("NPU 引擎不可用，回退到 heuristic")
            self._npu_fallback_count += 1
            if_score, ae_score, cnn_class, cnn_confidence = self._cpu_inference_fallback(features, packet)
            return if_score, ae_score, cnn_class, cnn_confidence, 'heuristic_fallback'

        try:
            from ai.npu_engine import ModelType
            
            # 准备输入数据
            feature_array = [features.values]
            raw_waveform = packet.channels[0][:256] if packet.channels else []

            # 执行 NPU 推理
            result_if = self._npu_engine.infer(ModelType.IFORREST, feature_array)
            result_ae = self._npu_engine.infer(ModelType.AE, np.array(feature_array))
            result_cnn = self._npu_engine.infer(ModelType.CNN1D, np.array(raw_waveform))

            # 提取输出
            if_output = self._safe_flatten(result_if.output)
            ae_output = self._safe_flatten(result_ae.output)
            cnn_output = self._safe_flatten(result_cnn.output)

            # === 结果有效性验证 ===
            if not self._validate_npu_outputs(if_output, ae_output, cnn_output):
                log.warning("NPU 输出验证失败 (无效值/NaN/Inf)，回退到 heuristic")
                self._npu_fallback_count += 1
                if_score, ae_score, cnn_class, cnn_confidence = self._cpu_inference_fallback(features, packet)
                return if_score, ae_score, cnn_class, cnn_confidence, 'heuristic_fallback'

            # 解析有效结果
            if_score = float(if_output[0])
            ae_score = float(ae_output[0])
            cnn_class = int(np.argmax(cnn_output))
            cnn_confidence = float(cnn_output[cnn_class])
            
            self._npu_fallback_count = 0  # 成功时重置计数器
            return if_score, ae_score, cnn_class, cnn_confidence, 'npu'

        except Exception as e:
            log.warning(f"NPU 推理异常: {e}, 回退到 heuristic")
            self._npu_fallback_count += 1
            if_score, ae_score, cnn_class, cnn_confidence = self._cpu_inference_fallback(features, packet)
            return if_score, ae_score, cnn_class, cnn_confidence, 'heuristic_fallback'

    @staticmethod
    def _validate_npu_outputs(if_output, ae_output, cnn_output) -> bool:
        """验证 NPU 输出的有效性"""
        # 检查输出是否为 None 或空
        if if_output is None or ae_output is None or cnn_output is None:
            return False
        if len(if_output) == 0 or len(ae_output) == 0 or len(cnn_output) == 0:
            return False

        # 检查是否包含 NaN 或 Inf
        if np.any(np.isnan(if_output)) or np.any(np.isinf(if_output)):
            return False
        if np.any(np.isnan(ae_output)) or np.any(np.isinf(ae_output)):
            return False
        if np.any(np.isnan(cnn_output)) or np.any(np.isinf(cnn_output)):
            return False

        # 检查 IF 分数是否在合理范围 [0, 1]
        if if_output[0] < -0.1 or if_output[0] > 1.1:
            return False

        # 检查 CNN 输出是否全为零或不合理
        if np.allclose(cnn_output, 0):
            return False  # 全零输出表示模型无效

        # 检查 CNN 置信度是否在合理范围
        cnn_conf = float(cnn_output[np.argmax(cnn_output)])
        if cnn_conf < 0 or cnn_conf > 1.1:
            return False

        return True

    @staticmethod
    def _safe_flatten(output) -> Optional[np.ndarray]:
        """安全地将输出转换为 numpy 数组"""
        try:
            if output is None:
                return None
            if isinstance(output, np.ndarray):
                return output.flatten()
            if isinstance(output, (list, tuple)):
                arr = np.array(output)
                return arr.flatten()
            return np.array([float(output)])
        except Exception:
            return None

    def _cpu_inference_fallback(self, features: FeatureVector,
                                packet: WaveformPacket) -> Tuple[float, float, int, float]:
        """CPU 回退推理 - 基于物理规则的确定性计算"""
        vals = features.values
        ua_rms = vals[0]
        ub_rms = vals[1]
        uc_rms = vals[2]

        # 计算三相电压统计
        mean_u = (ua_rms + ub_rms + uc_rms) / 3.0
        variance = sum((v - mean_u) ** 2 for v in [ua_rms, ub_rms, uc_rms]) / 3.0
        std_u = math.sqrt(variance)
        cv = std_u / mean_u if abs(mean_u) > 1e-6 else 0

        # === 改进的异常检测逻辑 ===
        
        # 1. 变异系数分数 (0-1)
        cv_score = min(cv * 2.0, 1.0)  # 放大系数，使 0.3 的 cv 变为 0.6
        
        # 2. 电压偏离正常值检测 (220V ± 20% 范围)
        # 正常范围: 176V - 264V
        ua_deviation = abs(ua_rms - 220.0) / 220.0
        ub_deviation = abs(ub_rms - 220.0) / 220.0
        uc_deviation = abs(uc_rms - 220.0) / 220.0
        max_deviation = max(ua_deviation, ub_deviation, uc_deviation)
        deviation_score = min(max_deviation * 3.0, 1.0)  # 放大系数
        
        # 3. 三相不平衡度检测
        if mean_u > 1e-6:
            unbalance_pct = std_u / mean_u * 100.0
        else:
            unbalance_pct = 0.0
        unbalance_score = min(unbalance_pct / 50.0, 1.0)  # 50% 对应满分
        
        # 4. 综合 IF 分数 (取最大值或加权)
        if_score = max(cv_score, deviation_score * 0.8, unbalance_score * 0.6)
        
        # 5. AE 分数 (基于 IF 分数和电压异常程度)
        ae_score = if_score * 1000.0 + (1.0 if if_score > 0.3 else 0.0) * 50.0
        
        # 6. CNN 分类
        if if_score > 0.6:
            cnn_class, cnn_confidence = 3, 0.90  # 严重异常
        elif if_score > 0.4:
            cnn_class, cnn_confidence = 2, 0.75  # 中度异常
        elif if_score > 0.25:
            cnn_class, cnn_confidence = 1, 0.65  # 轻度异常
        else:
            cnn_class, cnn_confidence = 0, 0.85  # 正常
        
        return if_score, ae_score, cnn_class, cnn_confidence

    def _determine_scene(self, if_score: float, cnn_class: int,
                          features: List[float]) -> str:
        """确定场景类型"""
        ua_rms = features[0] if len(features) > 0 else 0
        ub_rms = features[1] if len(features) > 1 else 0
        uc_rms = features[2] if len(features) > 2 else 0
        unbalance = features[12] if len(features) > 12 else 0

        if unbalance > 50:
            return 'S5'
        if unbalance > 10:
            return 'S4' if if_score > 0.5 else 'S2'
        if if_score > 0.5:
            return 'S3'
        return 'S1'

    # ========== 统计与查询接口 ==========

    def get_metrics(self) -> PipelineMetrics:
        """获取流水线性能指标"""
        uptime = time.time() - self._start_time
        throughput = self._total_packets / max(1, uptime) * 1000

        avg_total = sum(self._latency_samples) / max(1, len(self._latency_samples))
        avg_pre = sum(self._preprocess_samples) / max(1, len(self._preprocess_samples))
        avg_feat = sum(self._feature_samples) / max(1, len(self._feature_samples))
        avg_inf = sum(self._inference_samples) / max(1, len(self._inference_samples))

        target_met = sum(1 for l in self._latency_samples
                        if l <= MAX_INFERENCE_DELAY_MS)
        target_pct = target_met / max(1, len(self._latency_samples)) * 100

        return PipelineMetrics(
            total_packets=self._total_packets,
            total_anomalies=self._total_anomalies,
            avg_throughput=throughput,
            max_queue_depth=self._queue_max_depth,
            avg_preprocess_ms=avg_pre,
            avg_feature_ms=avg_feat,
            avg_inference_ms=avg_inf,
            avg_total_latency_ms=avg_total,
            latency_target_met_pct=target_pct,
            dropped_packets=self._dropped_packets,
            uptime_seconds=uptime,
        )

    def get_recent_results(self, count: int = 10) -> List[InferenceResult]:
        return list(self._result_cache)[-count:]

    def get_recent_anomalies(self, count: int = 10) -> List[InferenceResult]:
        return list(self._anomaly_cache)[-count:]

    def get_result_by_seq(self, cycle_seq: int) -> Optional[InferenceResult]:
        for result in self._result_cache:
            if result.cycle_seq == cycle_seq:
                return result
        return None

    def get_feature_by_seq(self, cycle_seq: int) -> Optional[FeatureVector]:
        for feat in self._feature_cache:
            if feat.cycle_seq == cycle_seq:
                return feat
        return None

    def get_trend(self, window: int = 50) -> Dict[str, Any]:
        """获取趋势分析数据"""
        results = list(self._result_cache)[-window:]
        if not results:
            return {}

        if_scores = [r.if_score for r in results]
        ae_scores = [r.ae_score for r in results]
        anomalies = [1 if r.is_anomaly else 0 for r in results]

        return {
            'window_size': len(results),
            'time_range': {
                'start': datetime.fromtimestamp(results[0].timestamp_us / 1_000_000).isoformat(),
                'end': datetime.fromtimestamp(results[-1].timestamp_us / 1_000_000).isoformat(),
            },
            'if_score_stats': {
                'min': round(min(if_scores), 4),
                'max': round(max(if_scores), 4),
                'avg': round(sum(if_scores) / len(if_scores), 4),
                'trend': 'up' if if_scores[-1] > if_scores[0] else ('down' if if_scores[-1] < if_scores[0] else 'stable'),
            },
            'anomaly_rate': round(sum(anomalies) / len(anomalies) * 100, 2),
            'latest_result': results[-1].to_dict(),
        }

    def clear_cache(self):
        """清空缓存"""
        self._result_cache.clear()
        self._feature_cache.clear()
        self._anomaly_cache.clear()
        self._total_packets = 0
        self._total_anomalies = 0
        self._dropped_packets = 0
        self._latency_samples.clear()
        self._preprocess_samples.clear()
        self._feature_samples.clear()
        self._inference_samples.clear()
        self._queue_max_depth = 0

    # ========== 辅助方法 ==========

    def _calc_rms(self, data: List[float]) -> float:
        if not data:
            return 0.0
        return math.sqrt(sum(v * v for v in data) / len(data))

    def _calc_thd_simple(self, data: List[float]) -> float:
        if not data or len(data) < 10:
            return 0.0
        rms = self._calc_rms(data)
        if rms < 1e-6:
            return 0.0
        try:
            n = len(data)
            freqs_to_check = [3, 5, 7, 9, 11]
            harmonic_power = 0
            for h in freqs_to_check:
                if h >= n // 2:
                    continue
                s = sum(data[i] * math.sin(2 * math.pi * h * i / n) for i in range(n))
                c = sum(data[i] * math.cos(2 * math.pi * h * i / n) for i in range(n))
                harmonic_power += (s * s + c * c) / (n * n)
            thd = math.sqrt(harmonic_power) / rms * 100
            return min(thd, 100)
        except Exception:
            return 0.0

    def _update_queue_depth(self):
        depth = max(self._q1.qsize(), self._q2.qsize(),
                     self._q3.qsize(), self._q4.qsize())
        self._queue_max_depth = max(self._queue_max_depth, depth)


# ========== 使用示例 ==========

if __name__ == '__main__':
    logging.basicConfig(level=logging.INFO, format='%(asctime)s [%(levelname)s] %(message)s')

    import numpy as np

    pipeline = WaveformPipeline()

    def on_result(result: InferenceResult):
        status = "异常" if result.is_anomaly else "正常"
        log.info(f"[{status}] seq={result.cycle_seq}, "
                 f"if={result.if_score:.4f}, "
                 f"cnn={result.cnn_class}, "
                 f"latency={result.latency_ms:.2f}ms")

    pipeline.add_result_callback(on_result)
    pipeline.start(num_workers=2)

    log.info("模拟数据注入...")
    for i in range(100):
        ua = 236.7 + np.random.randn(256) * 0.5
        ub = 1.2 + np.random.randn(256) * 0.3
        uc = 1.2 + np.random.randn(256) * 0.3
        ia = np.random.randn(256) * 0.0001
        ib = np.random.randn(256) * 0.0001
        ic = np.random.randn(256) * 0.0001

        packet = WaveformPacket(
            cycle_seq=i,
            timestamp_us=int(time.time() * 1_000_000),
            channels=[ua.tolist(), ub.tolist(), uc.tolist(),
                       ia.tolist(), ib.tolist(), ic.tolist(),
                       [0.0] * 256]
        )
        pipeline.submit_waveform(packet)
        time.sleep(0.005)

    time.sleep(2)

    metrics = pipeline.get_metrics()
    log.info(f"性能指标: {json.dumps(metrics.to_dict(), indent=2)}")

    trend = pipeline.get_trend(window=20)
    log.info(f"趋势: if_score均值={trend.get('if_score_stats', {}).get('avg', 'N/A')}")

    pipeline.stop()
