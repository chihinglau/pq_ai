#!/usr/bin/env python3
"""
LLM 集成与辅助决策模块

本模块实现 RK3576 上已部署 LLM 模型的推理接口, 为电能质量异常提供:
  1. 异常模式识别解释
  2. 故障诊断建议
  3. 预测性分析
  4. 治理方案生成

LLM 接口设计:
  - 支持本地 LLM (通过 HTTP API 或直接加载)
  - 与现有 AI 算法模块 (iForest/AE/CNN) 数据交互
  - Prompt 模板管理, 便于场景化定制
  - 响应解析与结构化输出

应用场景:
  - 电能质量异常的自然语言解释
  - 故障根因分析与建议
  - 基于历史数据的趋势预测
  - 运维决策辅助

@author PQ AI Terminal Team
@date 2026-08-13
"""

import os
import sys
import time
import json
import logging
import re
from typing import Optional, Dict, List, Tuple, Any
from dataclasses import dataclass, field
from enum import Enum
from datetime import datetime

log = logging.getLogger(__name__)

# ========== 配置 ==========
# 默认配置已针对 RK3576 部署优化
LLM_API_URL = os.environ.get('PQ_LLM_API_URL', 'http://127.0.0.1:8080/v1/chat/completions')
LLM_MODEL_NAME = os.environ.get('PQ_LLM_MODEL', 'qwen3-1.7b-rk3576.rkllm')
LLM_API_KEY = os.environ.get('PQ_LLM_API_KEY', '')
LLM_TIMEOUT_SEC = float(os.environ.get('PQ_LLM_TIMEOUT', '60.0'))
LLM_MAX_TOKENS = int(os.environ.get('PQ_LLM_MAX_TOKENS', '256'))

# ========== 数据类型 ==========

class LLMServiceType(Enum):
    EXPLANATION = 'explanation'
    DIAGNOSIS = 'diagnosis'
    PREDICTION = 'prediction'
    RECOMMENDATION = 'recommendation'


@dataclass
class PQFeatureSnapshot:
    ua_rms: float = 0.0
    ub_rms: float = 0.0
    uc_rms: float = 0.0
    ia_rms: float = 0.0
    ib_rms: float = 0.0
    ic_rms: float = 0.0
    ua_thd: float = 0.0
    ub_thd: float = 0.0
    uc_thd: float = 0.0
    unbalance_pct: float = 0.0
    frequency_hz: float = 50.0
    power_factor: float = 0.0
    if_score: float = 0.0
    ae_score: float = 0.0
    cnn_class: int = 0
    cnn_confidence: float = 0.0
    scene_id: str = 'S1'
    anomaly_detected: bool = False
    timestamp: str = ''

    def to_dict(self) -> Dict[str, Any]:
        return {
            'ua_rms': round(self.ua_rms, 2),
            'ub_rms': round(self.ub_rms, 2),
            'uc_rms': round(self.uc_rms, 2),
            'ia_rms': round(self.ia_rms, 4),
            'ib_rms': round(self.ib_rms, 4),
            'ic_rms': round(self.ic_rms, 4),
            'ua_thd': round(self.ua_thd, 2),
            'ub_thd': round(self.ub_thd, 2),
            'uc_thd': round(self.uc_thd, 2),
            'unbalance_pct': round(self.unbalance_pct, 2),
            'frequency_hz': round(self.frequency_hz, 2),
            'power_factor': round(self.power_factor, 4),
            'if_score': round(self.if_score, 4),
            'ae_score': round(self.ae_score, 4),
            'cnn_class': self.cnn_class,
            'cnn_confidence': round(self.cnn_confidence, 4),
            'scene_id': self.scene_id,
            'anomaly_detected': self.anomaly_detected,
            'timestamp': self.timestamp or datetime.now().isoformat(),
        }


@dataclass
class LLMResponse:
    service_type: LLMServiceType
    content: str
    structured_data: Dict[str, Any]
    latency_ms: float
    success: bool
    error_msg: str = ''
    tokens_used: int = 0


# ========== Prompt 模板 ==========

PROMPT_TEMPLATES = {
    LLMServiceType.EXPLANATION: {
        'system': '你是一名电能质量领域的资深专家。请分析以下电能质量数据，用专业但易懂的语言解释当前状态。',
        'user': """请分析以下电能质量监测数据并提供解释:

## 实时数据
{feature_json}

## 要求
1. 用2-3句话总结当前电能质量状态
2. 指出是否存在异常及其严重程度
3. 说明可能的影响范围
4. 给出1-2个关键关注点

请用JSON格式输出:
{{
  "summary": "状态总结",
  "severity": "normal/mild/moderate/severe",
  "impact_scope": "影响范围描述",
  "key_concerns": ["关注点1", "关注点2"]
}}""",
    },
    LLMServiceType.DIAGNOSIS: {
        'system': '你是一名电力系统故障诊断专家。根据电能质量数据分析可能的故障原因。',
        'user': """根据以下电能质量数据进行故障诊断:

## 实时数据
{feature_json}

## AI 推理结果
- 孤立森林异常得分: {if_score} (0-1, 越接近1越异常)
- 自编码器重构误差: {ae_score}
- 1D-CNN 事件分类: {cnn_class} (0=正常, 1=电压暂降, 2=电压暂升, 3=谐波, 4=不平衡, 5=过载, 6=瞬态)
- 分类置信度: {cnn_confidence}

## 要求
1. 基于数据分析最可能的故障原因
2. 按可能性从高到低列出3个潜在原因
3. 对每个原因给出排查方法
4. 评估故障的紧急程度

请用JSON格式输出:
{{
  "fault_cause": "最可能的故障原因",
  "probable_causes": [
    {{"cause": "原因1", "probability": 0.85, "check_method": "排查方法"}},
    {{"cause": "原因2", "probability": 0.45, "check_method": "排查方法"}},
    {{"cause": "原因3", "probability": 0.2, "check_method": "排查方法"}}
  ],
  "urgency": "low/medium/high/critical",
  "diagnosis_summary": "诊断总结"
}}""",
    },
    LLMServiceType.PREDICTION: {
        'system': '你是一名电能质量趋势预测专家。基于当前数据预测未来趋势。',
        'user': """基于以下电能质量数据进行趋势预测:

## 当前数据
{feature_json}

## 历史数据摘要
- 当前趋势: {trend_summary}
- 持续时间: {duration}
- 变化速率: {change_rate}

## 要求
1. 预测未来5分钟内电能质量趋势
2. 评估是否可能发生更严重的事件
3. 给出趋势反转的阈值条件
4. 标记需要关注的指标

请用JSON格式输出:
{{
  "trend_5min": "5分钟趋势描述",
  "escalation_risk": "low/medium/high",
  "thresholds_to_watch": [
    {{"metric": "指标名", "threshold": "阈值", "direction": "above/below"}}
  ],
  "predicted_values": {{
    "ua_rms": 230,
    "ub_rms": 220,
    ...
  }}
}}""",
    },
    LLMServiceType.RECOMMENDATION: {
        'system': '你是一名电能质量治理专家。根据异常情况提供治理建议。',
        'user': """根据以下电能质量异常数据，提供治理建议:

## 异常数据
{feature_json}

## AI 检测结果
- 异常类型: {anomaly_type}
- 严重程度: {severity}
- 持续时间: {duration}

## 现有措施
{existing_measures}

## 要求
1. 针对当前异常，按优先级给出3-5条治理建议
2. 每条建议包含: 措施描述、预期效果、实施难度、成本估算
3. 区分短期应急措施和长期优化方案
4. 评估各建议的风险

请用JSON格式输出:
{{
  "immediate_actions": [
    {{"action": "措施描述", "expected_effect": "预期效果", "difficulty": "low/medium/high", "cost": "low/medium/high"}}
  ],
  "long_term_solutions": [
    {{"solution": "方案描述", "benefit": "预期收益", "investment": "low/medium/high"}}
  ],
  "risk_assessment": "整体风险评估"
}}""",
    },
}


# ========== LLM 客户端 ==========

class LLMClient:
    """
    LLM 客户端, 支持 HTTP API 调用和本地模拟
    
    在 RK3576 上可对接已部署的 LLM 服务。
    在开发环境使用模拟响应。
    """

    def __init__(self, api_url: str = None, model_name: str = None):
        self.api_url = api_url or LLM_API_URL
        self.model_name = model_name or LLM_MODEL_NAME
        self.api_key = LLM_API_KEY
        self._available = self._check_availability()
        log.info(f"LLM 客户端初始化: model={self.model_name}, 可用={self._available}")

    def _check_availability(self) -> bool:
        if os.environ.get('PQ_LLM_SIMULATE', '0') == '1':
            log.info("LLM 模拟模式已启用")
            return False
        try:
            import urllib.request
            req = urllib.request.Request(
                self.api_url.replace('chat/completions', 'models'),
                method='GET'
            )
            req.add_header('Authorization', f'Bearer {self.api_key}')
            with urllib.request.urlopen(req, timeout=2) as resp:
                return resp.status == 200
        except Exception:
            log.info("LLM 服务不可用, 使用模拟响应")
            return False

    def call(self, service_type: LLMServiceType, 
             features: PQFeatureSnapshot,
             extra_context: Dict[str, Any] = None) -> LLMResponse:
        """调用 LLM 服务"""
        start = time.perf_counter()

        feature_json = json.dumps(features.to_dict(), ensure_ascii=False, indent=2)
        template = PROMPT_TEMPLATES.get(service_type)
        
        if template is None:
            return LLMResponse(
                service_type=service_type,
                content='',
                structured_data={},
                latency_ms=0,
                success=False,
                error_msg=f'未知的服务类型: {service_type.value}'
            )

        user_msg = self._fill_template(template['user'], features, extra_context)
        system_msg = template['system']

        if self._available:
            raw_response = self._call_api(system_msg, user_msg)
        else:
            raw_response = self._simulate_response(service_type, features)

        latency_ms = (time.perf_counter() - start) * 1000
        parsed = self._parse_response(raw_response, service_type)

        return LLMResponse(
            service_type=service_type,
            content=raw_response,
            structured_data=parsed,
            latency_ms=latency_ms,
            success=True
        )

    def _fill_template(self, template: str, features: PQFeatureSnapshot, 
                       extra: Dict[str, Any] = None) -> str:
        feature_json = json.dumps(features.to_dict(), ensure_ascii=False, indent=2)
        
        replacements = {
            '{feature_json}': feature_json,
            '{if_score}': f'{features.if_score:.4f}',
            '{ae_score}': f'{features.ae_score:.4f}',
            '{cnn_class}': str(features.cnn_class),
            '{cnn_confidence}': f'{features.cnn_confidence:.2f}',
            '{anomaly_type}': self._get_anomaly_type(features),
            '{severity}': self._get_severity(features),
            '{duration}': extra.get('duration', '持续监测中') if extra else '持续监测中',
            '{trend_summary}': extra.get('trend_summary', '当前数据稳定') if extra else '当前数据稳定',
            '{change_rate}': extra.get('change_rate', '稳定') if extra else '稳定',
            '{existing_measures}': extra.get('existing_measures', '暂无') if extra else '暂无',
        }
        if extra:
            for k, v in extra.items():
                if k not in replacements:
                    replacements[f'{{{k}}}'] = str(v)

        result = template
        for placeholder, value in replacements.items():
            result = result.replace(placeholder, value)
        return result

    def _get_anomaly_type(self, features: PQFeatureSnapshot) -> str:
        class_map = {0: '正常', 1: '电压暂降', 2: '电压暂升', 3: '谐波超标', 
                     4: '三相不平衡', 5: '过载', 6: '瞬态脉冲'}
        return class_map.get(features.cnn_class, '未知')

    def _get_severity(self, features: PQFeatureSnapshot) -> str:
        if features.if_score > 0.8:
            return '严重'
        if features.if_score > 0.5:
            return '中等'
        if features.if_score > 0.2:
            return '轻微'
        return '正常'

    def _call_api(self, system_msg: str, user_msg: str) -> str:
        try:
            import urllib.request
            payload = json.dumps({
                'model': self.model_name,
                'messages': [
                    {'role': 'system', 'content': system_msg},
                    {'role': 'user', 'content': user_msg},
                ],
                'max_tokens': LLM_MAX_TOKENS,
                'temperature': 0.3,
            }).encode('utf-8')

            req = urllib.request.Request(
                self.api_url,
                data=payload,
                headers={
                    'Content-Type': 'application/json',
                    'Authorization': f'Bearer {self.api_key}',
                }
            )
            with urllib.request.urlopen(req, timeout=LLM_TIMEOUT_SEC) as resp:
                data = json.loads(resp.read().decode('utf-8'))
                return data['choices'][0]['message']['content']
        except Exception as e:
            log.error(f"LLM API 调用失败: {e}")
            return self._simulate_response_raw(system_msg, user_msg)

    def _simulate_response(self, service_type: LLMServiceType, 
                            features: PQFeatureSnapshot) -> str:
        anomaly = features.anomaly_detected
        severity = self._get_severity(features)
        anomaly_type = self._get_anomaly_type(features)

        responses = {
            LLMServiceType.EXPLANATION: f'''{{
  "summary": "当前系统{severity}状态，{anomaly_type}。UA相电压{features.ua_rms:.1f}V，UB相{features.ub_rms:.1f}V，UC相{features.uc_rms:.1f}V。",
  "severity": "{self._severity_to_enum(severity)}",
  "impact_scope": "影响A相供电区域，B/C相暂不受影响",
  "key_concerns": ["B/C相电压异常偏低", "三相不平衡度需关注"]
}}''',
            LLMServiceType.DIAGNOSIS: f'''{{
  "fault_cause": "B/C相开路或断线",
  "probable_causes": [
    {{"cause": "B/C相断路器断开", "probability": 0.85, "check_method": "检查B/C相断路器状态"}},
    {{"cause": "B/C相线路断线", "probability": 0.35, "check_method": "使用红外测温检查线路连接点"}},
    {{"cause": "B/C相熔断器熔断", "probability": 0.15, "check_method": "检查熔断器状态"}}
  ],
  "urgency": "{self._urgency_level(features)}",
  "diagnosis_summary": "根据电压数据分析，B/C相电压仅为正常电压的0.5%，高度怀疑B/C相开路。"
}}''',
            LLMServiceType.PREDICTION: f'''{{
  "trend_5min": {{"ua_rms": {features.ua_rms + 1:.1f}, "ub_rms": {features.ub_rms:.1f}, "uc_rms": {features.uc_rms:.1f}}},
  "escalation_risk": "{self._risk_level(features)}",
  "thresholds_to_watch": [
    {{"metric": "B相电压", "threshold": "220V", "direction": "above"}},
    {{"metric": "C相电压", "threshold": "220V", "direction": "above"}}
  ],
  "predicted_values": {{"ua_rms": {features.ua_rms + 1:.1f}, "ub_rms": {features.ub_rms:.1f}, "uc_rms": {features.uc_rms:.1f}}}
}}''',
            LLMServiceType.RECOMMENDATION: f'''{{
  "immediate_actions": [
    {{"action": "立即检查B/C相断路器", "expected_effect": "恢复B/C相供电", "difficulty": "low", "cost": "low"}},
    {{"action": "检查B/C相线路连接", "expected_effect": "排除断线故障", "difficulty": "medium", "cost": "low"}}
  ],
  "long_term_solutions": [
    {{"solution": "增加B/C相电压监测点", "benefit": "快速定位异常", "investment": "low"}},
    {{"solution": "配置自动重合闸装置", "benefit": "异常自动恢复", "investment": "medium"}}
  ],
  "risk_assessment": "当前B/C相开路，存在三相不平衡风险，需尽快处理。"
}}''',
        }
        return responses.get(service_type, '{"error": "未知服务类型"}')

    def _simulate_response_raw(self, system: str, user: str) -> str:
        return json.dumps({
            "summary": "LLM 服务暂时不可用，使用模拟响应。",
            "severity": "mild",
            "diagnosis": "建议检查 LLM 服务状态",
        }, ensure_ascii=False)

    def _severity_to_enum(self, severity: str) -> str:
        mapping = {'严重': 'severe', '中等': 'moderate', '轻微': 'mild', '正常': 'normal'}
        return mapping.get(severity, 'normal')

    def _urgency_level(self, features: PQFeatureSnapshot) -> str:
        if features.if_score > 0.8:
            return 'critical'
        if features.if_score > 0.5:
            return 'high'
        if features.if_score > 0.2:
            return 'medium'
        return 'low'

    def _risk_level(self, features: PQFeatureSnapshot) -> str:
        if features.if_score > 0.7:
            return 'high'
        if features.if_score > 0.4:
            return 'medium'
        return 'low'

    def _parse_response(self, response_text: str, 
                        service_type: LLMServiceType) -> Dict[str, Any]:
        """解析 LLM 响应为结构化数据"""
        try:
            json_match = re.search(r'\{[\s\S]*\}', response_text)
            if json_match:
                return json.loads(json_match.group())
        except json.JSONDecodeError:
            pass

        return {
            'raw_text': response_text,
            'parsed': False,
            'service_type': service_type.value,
        }


# ========== 辅助决策管理器 ==========

class LLMAssistant:
    """
    LLM 辅助决策管理器
    
    整合 LLM 服务与现有 AI 算法, 提供统一的辅助决策接口。
    """

    def __init__(self, llm_client: LLMClient = None):
        self.client = llm_client or LLMClient()
        self._history: List[Dict[str, Any]] = []
        self._max_history = 100

    def analyze(self, features: PQFeatureSnapshot) -> Dict[str, Any]:
        """
        综合分析: 结合 AI 推理 + LLM 解释
        
        返回:
          - 状态总结
          - 异常解释 (LLM)
          - 故障诊断 (LLM)
          - 治理建议 (LLM, 可选)
          - 综合评估
        """
        result = {
            'timestamp': features.timestamp or datetime.now().isoformat(),
            'ai_result': {
                'if_score': features.if_score,
                'ae_score': features.ae_score,
                'cnn_class': features.cnn_class,
                'cnn_confidence': features.cnn_confidence,
                'anomaly': features.anomaly_detected,
            },
            'llm_explanation': None,
            'llm_diagnosis': None,
            'llm_recommendation': None,
            'overall_assessment': None,
        }

        if features.anomaly_detected:
            log.info("检测到异常, 调用 LLM 辅助决策...")
            
            explanation = self.client.call(
                LLMServiceType.EXPLANATION, features
            )
            result['llm_explanation'] = {
                'latency_ms': round(explanation.latency_ms, 1),
                'data': explanation.structured_data,
            }

            diagnosis = self.client.call(
                LLMServiceType.DIAGNOSIS, features
            )
            result['llm_diagnosis'] = {
                'latency_ms': round(diagnosis.latency_ms, 1),
                'data': diagnosis.structured_data,
            }

            if features.if_score > 0.5:
                recommendation = self.client.call(
                    LLMServiceType.RECOMMENDATION, features
                )
                result['llm_recommendation'] = {
                    'latency_ms': round(recommendation.latency_ms, 1),
                    'data': recommendation.structured_data,
                }

        result['overall_assessment'] = self._build_assessment(result)

        self._history.append(result)
        if len(self._history) > self._max_history:
            self._history = self._history[-self._max_history:]

        return result

    def _build_assessment(self, result: Dict[str, Any]) -> Dict[str, Any]:
        ai = result['ai_result']
        explanation = result.get('llm_explanation', {})
        diagnosis = result.get('llm_diagnosis', {})

        severity = 'normal'
        if ai['if_score'] > 0.8:
            severity = 'critical'
        elif ai['if_score'] > 0.5:
            severity = 'high'
        elif ai['if_score'] > 0.2:
            severity = 'medium'

        summary_parts = []
        summary_parts.append(f"AI判定: {'异常' if ai['anomaly'] else '正常'} "
                             f"(iForest={ai['if_score']:.2f}, CNN类别={ai['cnn_class']})")

        if explanation and explanation.get('data'):
            exp_data = explanation['data']
            if 'summary' in exp_data:
                summary_parts.append(f"LLM解释: {exp_data['summary']}")

        return {
            'severity': severity,
            'summary': ' | '.join(summary_parts),
            'has_llm_support': explanation is not None,
        }

    def get_history(self) -> List[Dict[str, Any]]:
        return self._history

    def clear_history(self):
        self._history.clear()


# ========== 便捷单例 ==========

_llm_assistant_instance: Optional[LLMAssistant] = None

def get_llm_assistant() -> LLMAssistant:
    global _llm_assistant_instance
    if _llm_assistant_instance is None:
        _llm_assistant_instance = LLMAssistant()
    return _llm_assistant_instance


if __name__ == '__main__':
    logging.basicConfig(level=logging.INFO, format='%(asctime)s [%(levelname)s] %(message)s')
    assistant = get_llm_assistant()

    test_features = PQFeatureSnapshot(
        ua_rms=236.7, ub_rms=1.2, uc_rms=1.2,
        ia_rms=0.0001, ib_rms=0.0001, ic_rms=0.0001,
        ua_thd=2.1, ub_thd=0.5, uc_thd=0.5,
        unbalance_pct=99.5,
        if_score=1.0, ae_score=0.7072,
        cnn_class=3, cnn_confidence=0.90,
        scene_id='S1', anomaly_detected=True,
    )

    result = assistant.analyze(test_features)
    print(json.dumps(result, ensure_ascii=False, indent=2))
