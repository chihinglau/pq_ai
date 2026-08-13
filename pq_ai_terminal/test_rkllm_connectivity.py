#!/usr/bin/env python3
"""
RKLLM 服务连通性验证脚本
========================
验证 RK3576 上的 RKLLM 大模型服务是否正常工作，
并测试 AI 推理服务是否能真正调用 RKLLM。

使用方法:
    python test_rkllm_connectivity.py                    # 使用默认配置
    python test_rkllm_connectivity.py --url http://127.0.0.1:8080
    python test_rkllm_connectivity.py --test-ai           # 同时测试 AI 推理服务

作者: PQ AI Terminal Team
日期: 2026-08-13
"""

import os
import sys
import json
import time
import argparse
import urllib.request
import urllib.error

# ========== 配置 ==========
DEFAULT_LLM_URL = os.environ.get('PQ_LLM_API_URL', 'http://127.0.0.1:8080/v1/chat/completions')
DEFAULT_MODEL = os.environ.get('PQ_LLM_MODEL', 'qwen3-1.7b-rk3576.rkllm')
DEFAULT_TIMEOUT = 30

# 颜色输出
RED = '\033[0;31m'
GREEN = '\033[0;32m'
YELLOW = '\033[1;33m'
CYAN = '\033[0;36m'
RESET = '\033[0m'


def log_ok(msg):
    print(f"{GREEN}[✓]{RESET} {msg}")


def log_err(msg):
    print(f"{RED}[✗]{RESET} {msg}")


def log_warn(msg):
    print(f"{YELLOW}[⚠]{RESET} {msg}")


def log_info(msg):
    print(f"{CYAN}[i]{RESET} {msg}")


def log_section(msg):
    print(f"\n{'='*60}")
    print(f"  {msg}")
    print(f"{'='*60}\n")


# ========== 测试函数 ==========

def test_health_check(base_url: str) -> dict:
    """测试健康检查接口"""
    log_info(f"测试健康检查: {base_url}/health")
    
    url = base_url.replace('v1/chat/completions', 'health')
    try:
        req = urllib.request.Request(url, method='GET')
        with urllib.request.urlopen(req, timeout=5) as resp:
            data = json.loads(resp.read().decode('utf-8'))
            log_ok(f"健康检查通过: status={data.get('status', 'unknown')}")
            return {'success': True, 'data': data}
    except urllib.error.URLError as e:
        log_err(f"健康检查失败: {e}")
        return {'success': False, 'error': str(e)}
    except Exception as e:
        log_err(f"健康检查异常: {e}")
        return {'success': False, 'error': str(e)}


def test_models_list(base_url: str) -> dict:
    """测试模型列表接口"""
    log_info(f"测试模型列表: {base_url}/models")
    
    url = base_url.replace('chat/completions', 'models')
    try:
        req = urllib.request.Request(url, method='GET')
        with urllib.request.urlopen(req, timeout=5) as resp:
            data = json.loads(resp.read().decode('utf-8'))
            models = data.get('data', [])
            model_ids = [m.get('id', 'unknown') for m in models]
            log_ok(f"获取模型列表成功: {model_ids}")
            return {'success': True, 'data': data, 'models': model_ids}
    except urllib.error.URLError as e:
        log_err(f"获取模型列表失败: {e}")
        return {'success': False, 'error': str(e)}
    except Exception as e:
        log_err(f"获取模型列表异常: {e}")
        return {'success': False, 'error': str(e)}


def test_chat_completions(base_url: str, model: str) -> dict:
    """测试聊天补全接口"""
    log_info(f"测试聊天补全: model={model}")
    
    url = base_url
    payload = {
        "model": model,
        "messages": [
            {"role": "system", "content": "你是一个有用的AI助手，请用一句话回答"},
            {"role": "user", "content": "1+1等于几？"}
        ],
        "max_tokens": 50,
        "temperature": 0.1
    }
    
    try:
        data = json.dumps(payload).encode('utf-8')
        req = urllib.request.Request(
            url,
            data=data,
            headers={'Content-Type': 'application/json'}
        )
        
        t0 = time.time()
        with urllib.request.urlopen(req, timeout=30) as resp:
            response = json.loads(resp.read().decode('utf-8'))
            elapsed = (time.time() - t0) * 1000
            
            # 解析响应
            choices = response.get('choices', [])
            if choices:
                content = choices[0].get('message', {}).get('content', '')
                log_ok(f"聊天补全成功: '{content.strip()}' (耗时: {elapsed:.0f}ms)")
                return {
                    'success': True,
                    'content': content,
                    'elapsed_ms': elapsed,
                    'usage': response.get('usage', {})
                }
            else:
                log_err("聊天补全响应为空")
                return {'success': False, 'error': '空响应'}
                
    except urllib.error.URLError as e:
        log_err(f"聊天补全失败: {e}")
        return {'success': False, 'error': str(e)}
    except Exception as e:
        log_err(f"聊天补全异常: {e}")
        return {'success': False, 'error': str(e)}


def test_llm_integration(script_dir: str) -> dict:
    """测试 AI 推理服务与 LLM 的集成"""
    log_info("测试 AI 推理服务与 LLM 集成...")
    
    try:
        # 添加项目路径
        sys.path.insert(0, script_dir)
        sys.path.insert(0, os.path.dirname(script_dir))
        
        from ai.llm_advisor import LLMClient, LLMServiceType, PQFeatureSnapshot
        
        # 创建 LLM 客户端
        client = LLMClient()
        
        if not client._available:
            log_err("LLM 服务不可用，将使用模拟响应")
            return {'success': False, 'error': 'LLM 服务不可用'}
        
        log_ok("LLM 客户端初始化成功，服务可用")
        
        # 创建测试特征快照
        snapshot = PQFeatureSnapshot(
            ua_rms=236.7, ub_rms=235.1, uc_rms=238.2,
            ia_rms=5.2, ib_rms=5.1, ic_rms=5.3,
            ua_thd=2.5, ub_thd=2.3, uc_thd=2.8,
            unbalance_pct=1.5,
            frequency_hz=50.0,
            power_factor=0.95,
            if_score=0.35,
            ae_score=0.42,
            cnn_class=0,
            cnn_confidence=0.98,
            scene_id='S1',
            anomaly_detected=False
        )
        
        # 测试异常解释
        log_info("测试异常解释 (EXPLANATION)...")
        response = client.call(LLMServiceType.EXPLANATION, snapshot)
        
        if response.success and response.content:
            log_ok(f"异常解释成功: {response.content[:100]}...")
            return {
                'success': True,
                'content': response.content,
                'latency_ms': response.latency_ms
            }
        else:
            log_err(f"异常解释失败: {response.error_msg}")
            return {'success': False, 'error': response.error_msg}
            
    except ImportError as e:
        log_err(f"导入模块失败: {e}")
        return {'success': False, 'error': str(e)}
    except Exception as e:
        log_err(f"LLM 集成测试异常: {e}")
        import traceback
        traceback.print_exc()
        return {'success': False, 'error': str(e)}


def test_streaming(base_url: str, model: str) -> dict:
    """测试流式输出"""
    log_info("测试流式输出...")
    
    url = base_url
    payload = {
        "model": model,
        "messages": [
            {"role": "user", "content": "说一句话"}
        ],
        "max_tokens": 50,
        "stream": True
    }
    
    try:
        data = json.dumps(payload).encode('utf-8')
        req = urllib.request.Request(
            url,
            data=data,
            headers={'Content-Type': 'application/json'}
        )
        
        t0 = time.time()
        full_content = ""
        chunk_count = 0
        
        with urllib.request.urlopen(req, timeout=15) as resp:
            for line in resp:
                line = line.decode('utf-8').strip()
                if line.startswith('data: '):
                    data_str = line[6:]
                    if data_str == '[DONE]':
                        break
                    try:
                        chunk = json.loads(data_str)
                        delta = chunk.get('choices', [{}])[0].get('delta', {})
                        if 'content' in delta:
                            full_content += delta['content']
                            chunk_count += 1
                    except json.JSONDecodeError:
                        pass
        
        elapsed = (time.time() - t0) * 1000
        log_ok(f"流式输出成功: {chunk_count} 个分片, 耗时: {elapsed:.0f}ms")
        return {
            'success': True,
            'content': full_content,
            'chunks': chunk_count,
            'elapsed_ms': elapsed
        }
        
    except Exception as e:
        log_err(f"流式输出失败: {e}")
        return {'success': False, 'error': str(e)}


# ========== 主程序 ==========

def main():
    parser = argparse.ArgumentParser(
        description='RKLLM 服务连通性验证',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python test_rkllm_connectivity.py                              # 全部测试
  python test_rkllm_connectivity.py --url http://127.0.0.1:8080   # 指定 URL
  python test_rkllm_connectivity.py --model qwen3-1.7b-rk3576.rkllm
  python test_rkllm_connectivity.py --skip-ai                     # 跳过 AI 集成测试
        """
    )
    parser.add_argument('--url', default=DEFAULT_LLM_URL,
                        help=f'RKLLM 服务 URL (默认: {DEFAULT_LLM_URL})')
    parser.add_argument('--model', default=DEFAULT_MODEL,
                        help=f'模型名称 (默认: {DEFAULT_MODEL})')
    parser.add_argument('--skip-ai', action='store_true',
                        help='跳过 AI 集成测试')
    parser.add_argument('--skip-stream', action='store_true',
                        help='跳过流式输出测试')
    parser.add_argument('--ai-only', action='store_true',
                        help='只测试 AI 集成')
    
    args = parser.parse_args()
    
    base_url = args.url
    model = args.model
    script_dir = os.path.dirname(os.path.abspath(__file__))
    
    print(f"\n{'='*60}")
    print(f"  RKLLM 服务连通性验证")
    print(f"{'='*60}")
    print(f"\n配置:")
    print(f"  URL:   {base_url}")
    print(f"  Model: {model}")
    print()
    
    results = {}
    all_passed = True
    
    if args.ai_only:
        # 只测试 AI 集成
        log_section("AI 推理服务集成测试")
        result = test_llm_integration(script_dir)
        results['ai_integration'] = result
        if not result['success']:
            all_passed = False
    else:
        # 1. 健康检查
        log_section("1. 健康检查")
        result = test_health_check(base_url)
        results['health'] = result
        if not result['success']:
            all_passed = False
        
        # 2. 模型列表
        log_section("2. 模型列表")
        result = test_models_list(base_url)
        results['models'] = result
        if not result['success']:
            all_passed = False
        
        # 3. 聊天补全
        log_section("3. 聊天补全测试")
        result = test_chat_completions(base_url, model)
        results['chat'] = result
        if not result['success']:
            all_passed = False
        
        # 4. 流式输出 (可选)
        if not args.skip_stream:
            log_section("4. 流式输出测试")
            result = test_streaming(base_url, model)
            results['stream'] = result
            if not result['success']:
                all_passed = False
        
        # 5. AI 集成测试 (可选)
        if not args.skip_ai:
            log_section("5. AI 推理服务集成测试")
            result = test_llm_integration(script_dir)
            results['ai_integration'] = result
            if not result['success']:
                all_passed = False
    
    # 汇总结果
    log_section("测试结果汇总")
    
    passed = sum(1 for r in results.values() if r.get('success'))
    total = len(results)
    
    print(f"\n通过: {passed}/{total}\n")
    
    for name, result in results.items():
        status = f"{GREEN}✓ 通过{RESET}" if result.get('success') else f"{RED}✗ 失败{RESET}"
        extra = ""
        if 'elapsed_ms' in result:
            extra = f" ({result['elapsed_ms']:.0f}ms)"
        print(f"  {status} {name}{extra}")
    
    print()
    
    if all_passed:
        log_ok("所有测试通过！RKLLM 服务工作正常。")
        log_info("AI 推理服务现在可以使用真实的 RKLLM 响应。")
        return 0
    else:
        log_err("部分测试失败，请检查 RKLLM 服务状态。")
        log_info("如需启动 RKLLM 服务，请运行:")
        log_info("  ./scripts/rk3576_ai_service.sh llm-start")
        return 1


if __name__ == '__main__':
    sys.exit(main())
