#!/usr/bin/env python3
"""
电能质量监测 AI 推理服务 (Integrated Inference Service)

整合三大AI模型推理: CNN1D + AE + iForest
支持 NPU 加速 / CPU 回退, LLM 辅助决策, 分层存储, 可靠通信协议

支持设备:
  - PC:      开发调试环境, CPU/GPU 推理
  - RK3576:  算力板部署, NPU 加速推理
  - T536:    采样终端, 数据采集与转发

运行模式:
  - service: 常驻服务模式, 接收实时波形数据
  - test:    批量测试模式, 生成模拟数据验证 pipeline
  - once:    单次推理模式, 执行一次推理后退出

运行示例:
  python integrated_inference_service.py
  python integrated_inference_service.py --device rk3576
  python integrated_inference_service.py --mode test --cycles 100
  python integrated_inference_service.py --mode once

@author PQ AI Terminal Team
@version 2.0.0
"""

import os, sys, time, json, signal, argparse, logging, threading, struct
import configparser, platform, traceback, socket, math
from datetime import datetime
from typing import Dict, Any, List, Optional

_app_dir = os.path.dirname(os.path.abspath(__file__))
_project_dir = os.path.dirname(_app_dir)
sys.path.insert(0, _project_dir)
sys.path.insert(0, _app_dir)

log = logging.getLogger(__name__)

SERVICE_VERSION = "2.0.0"
DEFAULT_CONFIG_PATH = os.path.join(_project_dir, "config.ini")
LOG_DIR = os.path.join(_project_dir, "logs")

_MAGIC_WAVE_RESULT = 0x57415645
_RESP_TYPE_OK = 0
_RESP_TYPE_ANOMALY = 2

_CNN_CLASS_MAP = {0: "正常", 1: "电压暂降", 2: "电压暂升", 3: "谐波",
    4: "三相不平衡", 5: "过载", 6: "瞬态脉冲"}

_SCENE_MAP = {"S1": "正常稳态", "S2": "轻微扰动", "S3": "暂态异常",
    "S4": "持续异常", "S5": "严重故障"}

class DeviceType:
    PC = "pc"
    RK3576 = "rk3576"
    T536 = "t536"
    ALL = [PC, RK3576, T536]
    @classmethod
    def display_name(cls, device):
        return {cls.RK3576: "RK3576 算力板", cls.T536: "T536 采样终端",
                cls.PC: "PC 开发环境"}.get(device, "未知设备")

class RunMode:
    SERVICE = "service"
    TEST = "test"
    ONCE = "once"
    ALL = [SERVICE, TEST, ONCE]


class IntegratedInferenceService:

    def __init__(self, device=DeviceType.PC, mode=RunMode.SERVICE,
                 config_path=None, host=None, port=None,
                 log_level="INFO", enable_npu=True, enable_llm=True,
                 enable_storage=True, enable_protocol=True,
                 enable_http=False, skip_self_check=False,
                 test_cycles=100, test_interval_ms=20,
                 wave_file=None, inference_mode='heuristic'):
        self.device = device
        self.mode = mode
        self.enable_npu = enable_npu
        self.enable_llm = enable_llm
        self.enable_storage = enable_storage
        self.enable_protocol = enable_protocol
        self.enable_http = enable_http
        self.skip_self_check = skip_self_check
        self.test_cycles = test_cycles
        self.test_interval_ms = test_interval_ms
        self.inference_mode = inference_mode  # 'heuristic' 或 'npu'
        self._running = False
        self._start_time = time.time()
        self._request_count = 0
        self._error_count = 0
        self._last_result_time = 0.0
        self._npu_engine = None
        self._llm_assistant = None
        self._storage = None
        self._pipeline = None
        self._protocol_server = None
        self._http_server = None
        self._wave_file = wave_file
        self._wave_data = None  # 存储加载的真实波形数据
        self._config = self._load_config(config_path)
        self._deploy_config = self._get_device_config()
        self.host = host or self._deploy_config.get("host") or self._get_default_host()
        self.port = port or int(self._deploy_config.get("port", "9090"))
        self._init_logging(log_level)
        self._self_check_result = None
        if not skip_self_check:
            self._self_check_result = self._run_self_check()
            self._log_self_check()
        self._init_modules()
        dn = DeviceType.display_name(device)
        log.info(f"服务初始化完成: v{SERVICE_VERSION}, 设备={dn}, 地址={self.host}:{self.port}")

    def _load_config(self, config_path=None):
        config = configparser.ConfigParser()
        candidates = []
        if config_path:
            candidates.append(config_path)
        candidates.extend([DEFAULT_CONFIG_PATH,
            os.path.join(_project_dir, "config.ini"),
            "/home/cat/pq_ai_v3/config.ini"])
        for path in candidates:
            if path and os.path.exists(path):
                try:
                    config.read(path, encoding="utf-8")
                    log.debug(f"配置已加载: {path}")
                    break
                except Exception as e:
                    log.warning(f"加载配置失败 {path}: {e}")
        return config

    def _get_device_config(self):
        sm = {DeviceType.RK3576: "rk3576_deploy",
              DeviceType.T536: "t536_terminal",
              DeviceType.PC: "pc_environment"}
        section = sm.get(self.device, "rk3576_deploy")
        if self._config.has_section(section):
            return dict(self._config.items(section))
        return {"host": "127.0.0.1", "port": "9090"}

    def _get_default_host(self):
        return {DeviceType.RK3576: "192.168.100.1",
                DeviceType.T536: "192.168.100.2",
                DeviceType.PC: "127.0.0.1"}.get(self.device, "127.0.0.1")

    def _init_logging(self, level):
        os.makedirs(LOG_DIR, exist_ok=True)
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        log_file = os.path.join(LOG_DIR, f"pq_ai_service_{ts}.log")
        lv = getattr(logging, level.upper(), logging.INFO)
        logging.basicConfig(level=lv,
            format="%(asctime)s [%(levelname)s] [%(name)s] %(message)s",
            datefmt="%Y-%m-%d %H:%M:%S",
            handlers=[logging.StreamHandler(sys.stdout),
                      logging.FileHandler(log_file, encoding="utf-8")])

    def _run_self_check(self):
        check = {"platform": platform.platform(), "machine": platform.machine(),
                 "python_version": platform.python_version(),
                 "npu_available": False, "npu_type": None,
                 "librknnrt_found": False, "models": [],
                 "port_available": None,
                 "config_loaded": self._config.has_section("ai"),
                 "dependencies": {"numpy": False}, "device_specific": {}}
        try:
            import numpy
            check["dependencies"]["numpy"] = True
            check["numpy_version"] = numpy.__version__
        except ImportError:
            log.warning("numpy 未安装, 部分功能受限")
        try:
            from rknnlite.api import RKNNLite
            check["npu_available"] = True
            check["npu_type"] = "rknnlite"
        except ImportError:
            try:
                from rknn.api import RKNN
                check["npu_available"] = True
                check["npu_type"] = "rknn"
            except ImportError:
                try:
                    from rknnsim.api import RKNN
                    check["npu_available"] = True
                    check["npu_type"] = "rknnsim"
                except ImportError:
                    pass
        for p in ["/usr/lib/librknnrt.so", "/usr/lib64/librknnrt.so",
                  "/usr/local/lib/librknnrt.so"]:
            if os.path.exists(p):
                check["librknnrt_found"] = True
                break
        model_dir = self._deploy_config.get("model_dir") or os.path.join(_project_dir, "models", "rknn")
        for fname, desc in [("cnn1d_event_classifier.rknn", "CNN1D 事件分类器"),
                              ("ae_anomaly_detector.rknn", "AE 自编码器"),
                              ("iforest_accelerated.rknn", "iForest 孤立森林")]:
            fp = os.path.join(model_dir, fname)
            exists = os.path.exists(fp)
            size = os.path.getsize(fp) if exists else 0
            check["models"].append({"name": fname, "desc": desc, "exists": exists,
                                     "size_kb": round(size / 1024, 1) if exists else 0})
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            s.bind((self.host, self.port))
            check["port_available"] = True
        except OSError:
            check["port_available"] = False
        finally:
            s.close()
        if self.device == DeviceType.RK3576:
            check["device_specific"]["usb_ecm_ip"] = self._deploy_config.get("usb_ip", "192.168.100.1")
        return check

    def _log_self_check(self):
        c = self._self_check_result
        log.info("=" * 50)
        log.info("启动自检 (v%s)" % SERVICE_VERSION)
        log.info("=" * 50)
        log.info("  平台:     %s (%s)" % (c["platform"], c["machine"]))
        log.info("  Python:   %s" % c["python_version"])
        has_np = c["dependencies"].get("numpy", False)
        log.info("  NumPy:    %s" % ("可用" if has_np else "不可用"))
        npu_ok = c["npu_available"]
        npu_t = c.get("npu_type", "N/A")
        log.info("  NPU:      %s (%s)" % ("可用" if npu_ok else "不可用", npu_t))
        log.info("  librknnrt: %s" % ("已找到" if c["librknnrt_found"] else "未找到"))
        for m in c["models"]:
            st = "Y" if m["exists"] else "N"
            sz = " (%sKB)" % m["size_kb"] if m["exists"] else ""
            log.info("  [%s] %s: %s%s" % (st, m["desc"], m["name"], sz))
        pa = c["port_available"]
        log.info("  端口:     %s (%s:%s)" % ("可用" if pa else "已占用", self.host, self.port))
        log.info("  配置加载: %s" % ("成功" if c["config_loaded"] else "使用默认值"))
        log.info("=" * 50)

    def _init_modules(self):
        if self.enable_npu: self._init_npu_engine()
        if self.enable_llm: self._init_llm_advisor()
        if self.enable_storage: self._init_storage()
        self._init_pipeline()
        if self.enable_protocol: self._init_protocol()
        if self.enable_http: self._init_http_server()

    def _init_npu_engine(self):
        try:
            from ai.npu_engine import NPUEngine, ComputeBackend
            self._npu_engine = NPUEngine(preferred_backend=ComputeBackend.AUTO)
            log.info(f"NPU 引擎: {self._npu_engine.get_backend().value}")
        except Exception as e:
            log.warning(f"NPU 初始化失败: {e}, 使用 CPU 回退")
            self._init_cpu_fallback()

    def _init_cpu_fallback(self):
        class SimpleEngine:
            def get_backend(self):
                class B: value = "cpu"
                return B()
            def get_performance_stats(self):
                return {"backend": "cpu", "models": {}}
            def infer(self, mt, inp):
                class R: output = None
                return R()
            def release(self): pass
        self._npu_engine = SimpleEngine()
        log.info("CPU 回退引擎已就绪")

    def _init_llm_advisor(self):
        try:
            from ai.llm_advisor import LLMAssistant
            self._llm_assistant = LLMAssistant()
            log.info("LLM 辅助决策模块已初始化")
        except Exception as e:
            log.warning(f"LLM 初始化失败: {e}")
            self._llm_assistant = None

    def _init_storage(self):
        try:
            from utils.storage_manager import TieredStorage
            self._storage = TieredStorage()
            log.info("分层存储管理器已初始化")
        except Exception as e:
            log.warning(f"存储初始化失败: {e}")
            self._storage = None

    def _init_pipeline(self):
        try:
            from app.wave_pipeline import WaveformPipeline, InferenceMode
            # 将字符串模式转换为枚举
            mode_map = {
                'heuristic': InferenceMode.HEURISTIC,
                'npu': InferenceMode.NPU
            }
            inference_mode = mode_map.get(self.inference_mode, InferenceMode.HEURISTIC)
            
            self._pipeline = WaveformPipeline(
                self._npu_engine, self._llm_assistant,
                inference_mode=inference_mode)
            if self._storage:
                self._pipeline.add_result_callback(self._on_result)
                self._pipeline.add_anomaly_callback(self._on_anomaly)
            log.info(f"波形处理流水线已初始化 (推理模式: {self.inference_mode})")
        except Exception as e:
            log.error(f"流水线初始化失败: {e}")

    def _init_protocol(self):
        try:
            from comm.protocol_v2 import ProtocolServer
            self._protocol_server = ProtocolServer(self.host, self.port)
            self._protocol_server.set_waveform_handler(self._on_waveform_data)
            log.info(f"协议服务端: {self.host}:{self.port}")
        except Exception as e:
            log.error(f"协议初始化失败: {e}")
            self._protocol_server = None

    def _init_http_server(self):
        try:
            from http.server import HTTPServer, BaseHTTPRequestHandler
            svc = self
            class H(BaseHTTPRequestHandler):
                def do_GET(self):
                    if self.path in ("/", "/status"): self._r(200, svc.get_full_status())
                    elif self.path == "/health": self._r(200, svc.health_check())
                    elif self.path == "/metrics": self._r(200, svc._get_metrics())
                    else: self._r(404, {"error": "not found"})
                def _r(self, code, data):
                    body = json.dumps(data, ensure_ascii=False).encode("utf-8")
                    self.send_response(code)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Content-Length", str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)
                def log_message(self, fmt, *a): pass
            hp = self.port + 10000
            self._http_server = HTTPServer(("0.0.0.0", hp), H)
            t = threading.Thread(target=self._http_server.serve_forever, daemon=True)
            t.start()
            log.info(f"HTTP 状态服务: 0.0.0.0:{hp}")
        except Exception as e:
            log.warning(f"HTTP 服务启动失败: {e}")
            self._http_server = None

    def start(self):
        dn = DeviceType.display_name(self.device)
        log.info(f"启动: {dn}, 模式={self.mode}")
        if self._wave_file:
            self._wave_data = self._load_comtrade_waveform(self._wave_file)
            if self._wave_data:
                log.info(f"已加载真实波形数据: {len(self._wave_data)} 个周期")
            else:
                log.warning("真实波形数据加载失败，使用模拟数据")
        if self.mode == RunMode.TEST: self._test_mode()
        elif self.mode == RunMode.ONCE: self._once_mode()
        else: self._service_mode()

    def stop(self):
        log.info("正在停止服务...")
        self._running = False
        if self._pipeline:
            try: self._pipeline.stop()
            except Exception as e: log.error(f"流水线停止异常: {e}")
        if self._storage:
            try: self._storage.stop()
            except Exception as e: log.error(f"存储停止异常: {e}")
        if self._protocol_server:
            try: self._protocol_server.stop()
            except Exception as e: log.error(f"协议停止异常: {e}")
        if self._npu_engine and hasattr(self._npu_engine, "release"):
            try: self._npu_engine.release()
            except Exception: pass
        log.info(f"服务已停止, 总请求={self._request_count}, 错误={self._error_count}")

    def _service_mode(self):
        if self._storage: self._storage.start()
        if self._pipeline: self._pipeline.start(num_workers=3)
        self._running = True
        if self._protocol_server:
            try: self._protocol_server.start()
            except Exception as e: log.error(f"协议启动失败: {e}")
        if not self._running: return
        self._status_thread = threading.Thread(target=self._status_loop, daemon=True)
        self._status_thread.start()
        log.info(f"服务已就绪: {self.host}:{self.port}")
        # 主循环，等待停止信号
        try:
            while self._running:
                time.sleep(1.0)
        except KeyboardInterrupt:
            log.info("收到键盘中断信号")

    def _test_mode(self):
        log.info(f"测试模式: {self.test_cycles} 轮, 间隔={self.test_interval_ms}ms")
        if self._storage: self._storage.start()
        if self._pipeline: self._pipeline.start(num_workers=2)
        errors = 0
        t0 = time.time()
        for i in range(self.test_cycles):
            try:
                channels = self._get_waveform_data(i)
                from app.wave_pipeline import WaveformPacket
                pkt = WaveformPacket(cycle_seq=i,
                    timestamp_us=int(time.time() * 1e6), channels=channels)
                if self._pipeline:
                    ok = self._pipeline.submit_waveform(pkt)
                    if ok: self._request_count += 1
                    else: errors += 1
                time.sleep(self.test_interval_ms / 1000.0)
            except Exception as e:
                self._error_count += 1; errors += 1
                if self._error_count <= 5: log.error(f"测试失败 #{i}: {e}")
        elapsed = time.time() - t0
        time.sleep(1.0)
        if self._pipeline:
            m = self._pipeline.get_metrics()
            log.info("=" * 50)
            log.info("测试完成报告")
            log.info("=" * 50)
            log.info(f"  总耗时:       {elapsed:.2f}s")
            log.info(f"  提交包数:     {self._request_count}")
            log.info(f"  流水线处理:   {m.total_packets}")
            log.info(f"  异常检测:     {m.total_anomalies}")
            log.info(f"  丢弃包数:     {m.dropped_packets}")
            log.info(f"  错误次数:     {errors}")
            log.info(f"  吞吐量:       {m.avg_throughput:.1f} pps")
            log.info(f"  平均延迟:     {m.avg_total_latency_ms:.2f} ms")
            log.info(f"  推理延迟:     {m.avg_inference_ms:.3f} ms")
            log.info(f"  延迟达标率:   {m.latency_target_met_pct:.1f}%")
            log.info("=" * 50)
            results = self._pipeline.get_recent_results(3)
            for r in results:
                cn = _CNN_CLASS_MAP.get(r.cnn_class, "?")
                log.info(f"  seq={r.cycle_seq}, if={r.if_score:.4f}, cnn={r.cnn_class}({cn}), lat={r.latency_ms:.2f}ms")
        self.stop()

    def _once_mode(self):
        log.info("单次推理测试")
        from app.wave_pipeline import WaveformPacket
        channels = self._gen_waveform(0, anomaly=True)
        pkt = WaveformPacket(cycle_seq=0,
            timestamp_us=int(time.time() * 1e6), channels=channels)
        if self._pipeline:
            self._pipeline.start(num_workers=1)
            self._pipeline.submit_waveform(pkt)
            self._request_count += 1  # 增加请求计数
            time.sleep(0.5)
            results = self._pipeline.get_recent_results(1)
            if results:
                r = results[0]
                log.info("=" * 50)
                log.info(f"  场景: {r.scene_id} ({_SCENE_MAP.get(r.scene_id, '?')})")
                log.info(f"  if={r.if_score:.4f}, ae={r.ae_score:.4f}, cnn={r.cnn_class}")
                log.info(f"  置信度={r.cnn_confidence:.4f}, 延迟={r.latency_ms:.2f}ms")
                log.info(f"  后端={r.backend}, 异常={r.is_anomaly}")
                log.info("=" * 50)
                if self._llm_assistant:
                    try:
                        from ai.llm_advisor import PQFeatureSnapshot
                        snap = PQFeatureSnapshot(if_score=r.if_score, ae_score=r.ae_score,
                            cnn_class=r.cnn_class, cnn_confidence=r.cnn_confidence,
                            scene_id=r.scene_id, anomaly_detected=r.is_anomaly)
                        lr = self._llm_assistant.analyze(snap)
                        oa = lr.get("overall_assessment", {}); log.info(f"  LLM: {json.dumps(oa, ensure_ascii=False)}")
                    except Exception as e: log.warning(f"LLM 失败: {e}")
            else: log.warning("未获取到推理结果")
        self.stop()

    def _gen_waveform(self, cycle=0, anomaly=False):
        import numpy as np
        channels = []
        for ch in range(7):
            t = np.linspace(0, 0.02, 256, endpoint=False)
            if ch < 3:
                base = 220 * np.sin(2 * np.pi * 50 * t)
                if anomaly and ch == 1: w = base * 0.005 + np.random.normal(0, 0.2, 256)
                elif anomaly and ch == 2: w = base * 0.008 + np.random.normal(0, 0.2, 256)
                else: w = base + np.random.normal(0, 0.5, 256)
            elif ch < 6:
                base = 5 * np.sin(2 * np.pi * 50 * t)
                if anomaly and ch == 4: w = base * 1.5 + np.random.normal(0, 0.3, 256)
                else: w = base + np.random.normal(0, 0.1, 256)
            else:
                w = np.random.normal(0, 0.1, 256)
            channels.append(w.tolist())
        return channels

    def _load_comtrade_waveform(self, cfg_path):
        """从 COMTRADE 文件加载真实波形数据"""
        import numpy as np
        import struct as st
        
        if not os.path.exists(cfg_path):
            log.error(f"COMTRADE 文件不存在: {cfg_path}")
            return None
        
        dat_path = cfg_path.replace('.cfg', '.dat')
        if not os.path.exists(dat_path):
            log.error(f"DAT 文件不存在: {dat_path}")
            return None
        
        log.info(f"加载 COMTRADE 文件: {cfg_path}")
        
        # 解析 CFG 文件
        with open(cfg_path, 'r', encoding='utf-8', errors='ignore') as f:
            lines = [line.strip() for line in f.readlines()]
        
        # Line 1: station,recorder_id,version
        parts = lines[0].split(',')
        log.info(f"  站点: {parts[0]}, 版本: {parts[2]}")
        
        # Line 2: total_channels, analog, digital
        parts = lines[1].split(',')
        total_channels = int(parts[0].strip())
        num_analog = int(parts[1].replace('A', '').strip())
        num_digital = int(parts[2].replace('D', '').strip())
        
        # 解析通道信息
        channel_names = []
        channel_ratios = []
        for i in range(num_analog):
            line = lines[2 + i]
            ch_parts = line.split(',')
            channel_names.append(ch_parts[1].strip())
            channel_ratios.append(float(ch_parts[5].strip()))
        
        # 频率
        frequency = float(lines[2 + num_analog].strip())
        
        # 采样率
        num_rates = int(lines[3 + num_analog].strip())
        rate_idx = 4 + num_analog
        for i in range(num_rates):
            rate_line = lines[rate_idx + i].strip()
            rate, samples = rate_line.split(',')
            if i == 0:
                sampling_rate = float(rate.strip())
                num_samples = int(samples.strip())
        
        log.info(f"  通道数: {num_analog}, 频率: {frequency}Hz, 采样率: {sampling_rate}Hz, 样本数: {num_samples}")
        log.info(f"  通道: {channel_names}")
        
        # 读取 DAT 文件
        dig_bytes = (num_digital + 15) // 16 * 2
        rec_size = 4 + num_analog * 2 + dig_bytes
        
        with open(dat_path, 'rb') as f:
            raw = f.read()
        
        actual_records = len(raw) // rec_size
        
        # 尝试 float64 时间戳
        if actual_records == 0:
            rec_size = 8 + num_analog * 2 + dig_bytes
            actual_records = len(raw) // rec_size
            ts_size = 8
            ts_fmt = '<d'
        else:
            ts_size = 4
            ts_fmt = '<f'
        
        log.info(f"  记录数: {actual_records}")
        
        # 读取数据
        data = np.zeros((num_analog, actual_records))
        for i in range(actual_records):
            offset = i * rec_size
            for ch in range(num_analog):
                val = st.unpack_from('<h', raw, offset + ts_size + ch * 2)[0]
                data[ch, i] = float(val)
        
        # 应用比例转换为实际值
        for ch in range(num_analog):
            data[ch] = data[ch] * channel_ratios[ch]
        
        # 截取 256 点作为一个周期
        points_per_cycle = int(sampling_rate / frequency)
        log.info(f"  每周期点数: {points_per_cycle}")
        
        # 提取多个周期的波形数据
        waveforms = []
        num_cycles = actual_records // points_per_cycle
        
        for cycle in range(min(num_cycles, 100)):  # 最多提取 100 个周期
            start = cycle * points_per_cycle
            end = start + points_per_cycle
            if end > actual_records:
                break
            
            # 只取前 7 个通道 (UA, UB, UC, IA, IB, IC, IZ)
            cycle_data = []
            for ch_idx in range(min(7, num_analog)):
                ch_data = data[ch_idx, start:end]
                # 重采样到 256 点
                if len(ch_data) >= 256:
                    resampled = np.interp(np.linspace(0, len(ch_data)-1, 256), 
                                        np.arange(len(ch_data)), ch_data)
                else:
                    resampled = np.pad(ch_data, (0, 256 - len(ch_data)))
                cycle_data.append(resampled.tolist())
            
            # 如果通道数不够 7，用 0 填充
            while len(cycle_data) < 7:
                cycle_data.append([0.0] * 256)
            
            waveforms.append(cycle_data)
        
        log.info(f"  提取周期数: {len(waveforms)}")
        return waveforms

    def _get_waveform_data(self, cycle=0):
        """获取波形数据，优先使用 COMTRADE 真实数据"""
        if self._wave_data and len(self._wave_data) > 0:
            idx = cycle % len(self._wave_data)
            return self._wave_data[idx]
        return self._gen_waveform(cycle, anomaly=False)

    def _on_waveform_data(self, data, seq):
        t0 = time.time()
        try:
            off = 0
            nc = struct.unpack_from("<H", data, off)[0]; off += 2
            ppc = struct.unpack_from("<I", data, off)[0]; off += 4
            cs = struct.unpack_from("<I", data, off)[0]; off += 4
            ts = struct.unpack_from("<Q", data, off)[0]; off += 8
            channels = []
            for c in range(nc):
                cd = [struct.unpack_from("<f", data, off + i * 4)[0] for i in range(ppc)]
                off += ppc * 4
                channels.append(cd)
            from app.wave_pipeline import WaveformPacket
            pkt = WaveformPacket(cycle_seq=cs, timestamp_us=ts, channels=channels)
            parse_us = (time.time() - t0) * 1000
            if self._pipeline:
                ok = self._pipeline.submit_waveform(pkt)
                if ok:
                    self._request_count += 1
                    log.debug("[数据接收] seq=%d, 通道=%d, 采样点=%d, 解析耗时=%.3fms, 数据包=%d字节",
                              cs, nc, ppc, parse_us, len(data))
                else:
                    log.warning("[数据接收] 波形队列已满, 丢弃 seq=%d, 解析耗时=%.3fms", cs, parse_us)
        except Exception as e:
            log.error("[数据接收] 波形解析失败: seq=%d, 错误=%s, 数据包=%d字节",
                      seq, e, len(data)); self._error_count += 1

    def _on_result(self, result):
        t0 = time.time()
        self._last_result_time = time.time()
        ifs = getattr(result, "if_score", 0)
        cnn_cls = getattr(result, "cnn_class", 0)
        cnn_cls_name = _CNN_CLASS_MAP.get(cnn_cls, "?")
        is_anom = getattr(result, "is_anomaly", False)
        
        log.info("[结果处理] seq=%d, IF=%.4f, AE=%.4f, CNN=%d(%s), 置信度=%.4f, 场景=%s, 后端=%s, 总延迟=%.2fms, 异常=%s",
                 result.cycle_seq, ifs, getattr(result, "ae_score", 0),
                 cnn_cls, cnn_cls_name, getattr(result, "cnn_confidence", 0),
                 getattr(result, "scene_id", "?"), getattr(result, "backend", "?"),
                 getattr(result, "latency_ms", 0), is_anom)
        
        if self._storage:
            from utils.storage_manager import ImportanceLevel
            if ifs > 0.8: imp = ImportanceLevel.CRITICAL
            elif ifs > 0.5: imp = ImportanceLevel.HIGH
            elif ifs > 0.2: imp = ImportanceLevel.MEDIUM
            else: imp = ImportanceLevel.LOW
            try:
                self._storage.store_result(result, imp)
                log.debug("[结果处理] 存储完成 seq=%d, 重要性=%s, 耗时=%.3fms",
                          result.cycle_seq, imp.name, (time.time() - t0) * 1000)
            except Exception as e:
                log.error("[结果处理] 存储失败 seq=%d: %s", result.cycle_seq, e)
        
        if self._protocol_server:
            try:
                resp = self._build_response(result)
                self._protocol_server.send_ai_result(resp)
                log.debug("[结果处理] AI响应已发送 seq=%d, 包长=%d字节", result.cycle_seq, len(resp))
            except Exception as e:
                log.error("[结果处理] 响应失败 seq=%d: %s", result.cycle_seq, e)

    def _on_anomaly(self, result):
        cn = _CNN_CLASS_MAP.get(getattr(result, "cnn_class", -1), "?")
        log.warning("[异常检测] seq=%d, IF=%.4f, CNN=%d(%s), 置信度=%.4f, 场景=%s, 总延迟=%.2fms",
                    result.cycle_seq, result.if_score, result.cnn_class, cn,
                    getattr(result, "cnn_confidence", 0),
                    getattr(result, "scene_id", "?"),
                    getattr(result, "latency_ms", 0))
        
        if self._llm_assistant and self.enable_llm:
            log.info("[异常检测] 启动LLM根因分析 seq=%d", result.cycle_seq)
            try:
                self._run_llm_analysis(result)
            except Exception as e:
                log.error("[异常检测] LLM分析失败 seq=%d: %s", result.cycle_seq, e)

    def _run_llm_analysis(self, result):
        t0 = time.time()
        from ai.llm_advisor import PQFeatureSnapshot
        channels = getattr(result, "channels", [])
        ua = ub = uc = 0.0
        if channels and len(channels) >= 3:
            ua = math.sqrt(sum(v*v for v in channels[0]) / max(1, len(channels[0])))
            ub = math.sqrt(sum(v*v for v in channels[1]) / max(1, len(channels[1])))
            uc = math.sqrt(sum(v*v for v in channels[2]) / max(1, len(channels[2])))
        snap = PQFeatureSnapshot(ua_rms=ua, ub_rms=ub, uc_rms=uc,
            if_score=result.if_score, ae_score=result.ae_score,
            cnn_class=result.cnn_class, cnn_confidence=result.cnn_confidence,
            scene_id=result.scene_id, anomaly_detected=result.is_anomaly)
        
        log.debug("[LLM决策] seq=%d, Ua=%.2f, Ub=%.2f, Uc=%.2f, IF=%.4f, 场景=%s",
                  result.cycle_seq, ua, ub, uc, result.if_score, result.scene_id)
        
        analysis = self._llm_assistant.analyze(snap)
        llm_time = (time.time() - t0) * 1000
        oa2 = analysis.get("overall_assessment", {})
        log.info("[LLM决策] seq=%d, 分析耗时=%.3fms, 评估=%s, 详情=%s",
                 result.cycle_seq, llm_time,
                 oa2.get("assessment", "?"),
                 json.dumps(oa2, ensure_ascii=False)[:200])
        return analysis

    def _build_response(self, result):
        resp = bytearray()
        resp.extend(struct.pack("<I", _MAGIC_WAVE_RESULT))
        rt = _RESP_TYPE_ANOMALY if result.is_anomaly else _RESP_TYPE_OK
        resp.extend(struct.pack("<B", rt))
        resp.extend(struct.pack("<Q", result.timestamp_us))
        resp.extend(struct.pack("<I", result.cycle_seq))
        resp.extend(struct.pack("<f", result.if_score))
        resp.extend(struct.pack("<f", result.ae_score))
        resp.extend(struct.pack("<f", result.cnn_confidence))
        resp.extend(struct.pack("<B", result.cnn_class))
        sid = 0
        for k, v in _SCENE_MAP.items():
            if v == result.scene_id: break
            sid += 1
        resp.extend(struct.pack("<B", sid))
        crc = self._crc32_calc(bytes(resp))
        resp.extend(struct.pack("<I", crc))
        return bytes(resp)

    @staticmethod
    def _crc32_calc(data):
        import zlib
        return zlib.crc32(data) & 0xFFFFFFFF

    def _status_loop(self):
        while self._running:
            time.sleep(5.0)
            try:
                metrics = self._get_metrics()
                if metrics.get("status") != "running": break
                req_ct = metrics['requests']; err_ct = metrics['errors']; lat = metrics.get('avg_latency_ms', 0); thr = metrics.get('throughput', 0); log.info(f"状态: req={req_ct}, err={err_ct}, latency={lat:.2f}ms, throughput={thr:.1f}pps")
            except Exception as e: log.error(f"状态循环异常: {e}")

    def _get_metrics(self):
        now = time.time()
        uptime = int(now - self._start_time)
        pipeline_metrics = {}
        if self._pipeline:
            try:
                m = self._pipeline.get_metrics()
                pipeline_metrics = {
                    "total_packets": m.total_packets,
                    "total_anomalies": m.total_anomalies,
                    "dropped_packets": m.dropped_packets,
                    "avg_throughput": m.avg_throughput,
                    "avg_latency_ms": m.avg_total_latency_ms,
                    "avg_inference_ms": m.avg_inference_ms,
                    "avg_ae_ms": m.avg_ae_ms,
                    "avg_if_ms": m.avg_if_ms,
                    "cnn_calls": m.cnn_calls,
                    "ae_calls": m.ae_calls,
                    "if_calls": m.if_calls,
                    "latency_target_met_pct": m.latency_target_met_pct,
                }
            except Exception: pass
        return {
            "status": "running" if self._running else "stopped",
            "service_version": SERVICE_VERSION,
            "device": self.device,
            "device_display": DeviceType.display_name(self.device),
            "mode": self.mode,
            "host": self.host,
            "port": self.port,
            "uptime_seconds": uptime,
            "uptime_human": self._fmt_uptime(uptime),
            "requests": self._request_count,
            "errors": self._error_count,
            "last_result_time": self._last_result_time,
            "pipeline": pipeline_metrics,
            "modules": {
                "npu": self._npu_engine is not None,
                "llm": self._llm_assistant is not None,
                "storage": self._storage is not None,
                "pipeline": self._pipeline is not None,
                "protocol": self._protocol_server is not None,
                "http": self._http_server is not None,
            },
            "npu_backend": self._npu_engine.get_backend().value if self._npu_engine and hasattr(self._npu_engine, "get_backend") else "none",
        }

    @staticmethod
    def _fmt_uptime(sec):
        d = sec // 86400; h = (sec % 86400) // 3600
        m = (sec % 3600) // 60; s = sec % 60
        if d > 0: return f"{d}d {h}h {m}m {s}s"
        elif h > 0: return f"{h}h {m}m {s}s"
        elif m > 0: return f"{m}m {s}s"
        return f"{s}s"

    def get_full_status(self):
        return self._get_metrics()

    def health_check(self):
        metrics = self._get_metrics()
        checks = {}
        checks["npu_engine"] = metrics["modules"]["npu"]
        checks["pipeline"] = metrics["modules"]["pipeline"]
        checks["storage"] = metrics["modules"]["storage"]
        checks["protocol"] = metrics["modules"]["protocol"]
        all_ok = all(checks.values())
        return {
            "status": "healthy" if all_ok and self._running else "unhealthy",
            "checks": checks,
            "uptime_seconds": metrics["uptime_seconds"],
            "requests_total": metrics["requests"],
            "errors_total": metrics["errors"],
            "device": self.device,
            "service_version": SERVICE_VERSION,
        }

def main():
    parser = argparse.ArgumentParser(
        description=f"电能质量监测 AI 推理服务 v{SERVICE_VERSION}",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""示例:
  python integrated_inference_service.py
  python integrated_inference_service.py --device rk3576 --host 192.168.100.1
  python integrated_inference_service.py --mode test --cycles 200 --interval 10
  python integrated_inference_service.py --mode once
  python integrated_inference_service.py --pc --once
""")
    parser.add_argument("-d", "--device", choices=DeviceType.ALL,
                        default=DeviceType.PC, help="目标设备类型")
    parser.add_argument("-m", "--mode", choices=RunMode.ALL,
                        default=RunMode.SERVICE, help="运行模式")
    parser.add_argument("--host", default=None, help="绑定 IP 地址")
    parser.add_argument("--port", type=int, default=None, help="服务端口")
    parser.add_argument("-c", "--config", default=None, help="配置文件路径")
    parser.add_argument("-l", "--log-level", default="INFO",
                        choices=["DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"])
    parser.add_argument("--no-npu", action="store_true", help="禁用 NPU, 仅 CPU 推理")
    parser.add_argument("--no-llm", action="store_true", help="禁用 LLM 辅助决策")
    parser.add_argument("--no-storage", action="store_true", help="禁用持久化存储")
    parser.add_argument("--no-protocol", action="store_true", help="禁用通信协议服务")
    parser.add_argument("--http", action="store_true", help="启用 HTTP 状态服务")
    parser.add_argument("--no-self-check", action="store_true", help="跳过启动自检")
    parser.add_argument("--cycles", type=int, default=100, help="测试模式周期数")
    parser.add_argument("--interval", type=int, default=20, help="测试间隔(ms)")
    parser.add_argument("--timeout", type=int, default=0, help="运行超时(s, 0=无限)")
    parser.add_argument("--version", action="store_true", help="显示版本")
    parser.add_argument("--pc", action="store_true", help="PC 开发环境快捷选项")
    parser.add_argument("--rk3576", action="store_true", help="RK3576 部署快捷选项")
    parser.add_argument("--once", action="store_true", help="单次推理快捷选项")
    parser.add_argument("--test", action="store_true", help="测试模式快捷选项")
    parser.add_argument("--inference-mode", choices=["heuristic", "npu"],
                        default="heuristic",
                        help="推理模式: heuristic=物理启发式(默认,稳定可靠), npu=NPU AI推理(需模型训练)")
    parser.add_argument("--wave-file", default=None, help="COMTRADE 波形文件 (.cfg) 路径")
    args = parser.parse_args()
    if args.version:
        print(f"PQ AI Inference Service v{SERVICE_VERSION}")
        return
    if args.pc: args.device = DeviceType.PC
    if args.rk3576: args.device = DeviceType.RK3576
    if args.once: args.mode = RunMode.ONCE
    if args.test: args.mode = RunMode.TEST
    log.info("=" * 60)
    log.info(f"  电能质量 AI 推理服务 v{SERVICE_VERSION}")
    log.info(f"  设备: {DeviceType.display_name(args.device)}")
    log.info(f"  模式: {args.mode}")
    cfg = args.config or '默认'; log.info(f"  配置: {cfg}")
    ts = datetime.now().strftime('%Y-%m-%d %H:%M:%S'); log.info(f"  时间: {ts}")
    log.info("=" * 60)
    svc = IntegratedInferenceService(
        device=args.device, mode=args.mode, config_path=args.config,
        host=args.host, port=args.port, log_level=args.log_level,
        enable_npu=not args.no_npu, enable_llm=not args.no_llm,
        enable_storage=not args.no_storage, enable_protocol=not args.no_protocol,
        enable_http=args.http, skip_self_check=args.no_self_check,
        test_cycles=args.cycles, test_interval_ms=args.interval,
        inference_mode=args.inference_mode)
    try:
        svc.start()
    except KeyboardInterrupt:
        log.info("收到中断信号")
    except Exception as e:
        log.error(f"服务异常: {e}")
        traceback.print_exc()
    finally:
        svc.stop()

if __name__ == "__main__":
    main()