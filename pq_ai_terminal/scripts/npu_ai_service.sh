#!/bin/bash
#
# RK3576 NPU 推理服务管理
# =====================
# 与 RKLLM 共存版本 - 保障两个 NPU 模型同时运行且互不干扰
#
# 用法:
#   ./npu_ai_service.sh start        # 启动 NPU 推理服务
#   ./npu_ai_service.sh stop         # 停止 NPU 推理服务
#   ./npu_ai_service.sh restart       # 重启 NPU 推理服务
#   ./npu_ai_service.sh status       # 查看服务状态
#   ./npu_ai_service.sh logs [N]     # 查看日志 (默认50行)
#   ./npu_ai_service.sh test         # 测试 NPU 模型加载
#   ./npu_ai_service.sh benchmark    # NPU 推理性能测试
#   ./npu_ai_service.sh check-all    # 检查所有 NPU 服务状态
#

set -e

# ========== 配置 ==========
APP_DIR="/home/cat/pq_ai_v3"
LOG_DIR="${APP_DIR}/logs"
PID_FILE="${LOG_DIR}/npu_ai_service.pid"
MODEL_DIR="${APP_DIR}/models"
SCRIPT_DIR="${APP_DIR}/app"

# NPU 服务配置
SERVICE_NAME="wave_inference_server_v5_npu.py"
SERVICE_PORT=9090
SERVICE_HOST="192.168.100.1"
MODEL_FILE="${MODEL_DIR}/cnn1d_8class.rknn"

# NPU 核心绑定配置 (与 RKLLM 共存)
# RK3576 有 2 个 NPU 核心: 0, 1
# RKLLM 使用核心 0 (默认)，RKNN 推理绑定核心 1
NPU_CORE_MASK="${NPU_CORE_MASK:-core1}"

# RKLLM 服务配置 (用于冲突检测)
LLM_SERVICE_NAME="rkllm-server"
LLM_PORT=8080
LLM_HEALTH_URL="http://127.0.0.1:${LLM_PORT}/health"

# 资源阈值
MAX_NPU_USAGE_WARN=80
MAX_MEMORY_USAGE_WARN=85
MAX_CPU_USAGE_WARN=90

# 启动等待超时 (秒)
LLM_WAIT_TIMEOUT=60
NPU_INIT_TIMEOUT=30

# 颜色
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
RESET='\033[0m'

log_info() { echo -e "${CYAN}[INFO]${RESET} $(date '+%Y-%m-%d %H:%M:%S') $*"; }
log_ok() { echo -e "${GREEN}[OK]${RESET} $*"; }
log_warn() { echo -e "${YELLOW}[WARN]${RESET} $*"; }
log_err() { echo -e "${RED}[ERROR]${RESET} $*"; }
log_llm() { echo -e "${MAGENTA}[LLM]${RESET} $*"; }

# ========== 资源检测 ==========

# 检查 RKLLM 服务状态
check_rkllm_status() {
    local status=0
    
    # 方法1: 检查 systemctl
    if command -v systemctl &> /dev/null; then
        if systemctl is-active --quiet "${LLM_SERVICE_NAME}" 2>/dev/null; then
            log_llm "RKLLM 服务运行中 (systemctl: active)"
            return 0
        fi
    fi
    
    # 方法2: 检查 HTTP 健康接口
    local response=$(curl -s --connect-timeout 2 --max-time 3 "${LLM_HEALTH_URL}" 2>/dev/null || true)
    if echo "${response}" | grep -q '"status"'; then
        local llm_status=$(echo "${response}" | grep -o '"status":"[^"]*"' | cut -d'"' -f4)
        if [ "${llm_status}" = "ready" ]; then
            log_llm "RKLLM 服务就绪 (HTTP: ready)"
            return 0
        else
            log_llm "RKLLM 服务状态: ${llm_status} (HTTP)"
            return 2
        fi
    fi
    
    # 方法3: 检查进程
    local llm_procs=$(pgrep -f "rkllm\|llama-cli" 2>/dev/null || true)
    if [ -n "${llm_procs}" ]; then
        log_llm "RKLLM 相关进程存在: ${llm_procs}"
        return 0
    fi
    
    log_llm "RKLLM 服务未运行"
    return 1
}

# 检查 NPU 资源状态
check_npu_resources() {
    local npu_usage=0
    local mem_usage=0
    
    # 检查 NPU 负载
    if [ -f "/sys/kernel/debug/rknpu/load" ]; then
        local npu_load=$(cat /sys/kernel/debug/rknpu/load 2>/dev/null || echo "0")
        # 计算平均 NPU 使用率
        npu_usage=$(echo "${npu_load}" | awk '{sum=0; n=split($0,a," "); for(i=1;i<=n;i++) sum+=a[i]; print int(sum/n)}' 2>/dev/null || echo "0")
    fi
    
    # 检查内存使用
    if command -v free &> /dev/null; then
        mem_usage=$(free | grep Mem | awk '{printf "%.0f", $3/$2 * 100}' 2>/dev/null || echo "0")
    fi
    
    echo "${npu_usage} ${mem_usage}"
}

# 检查是否存在 NPU 冲突风险
check_npu_conflict() {
    log_info "检查 NPU 资源状态..."
    
    local rkllm_running=1
    check_rkllm_status || rkllm_running=$?
    
    local npu_mem_info=$(check_npu_resources)
    local npu_usage=$(echo "${npu_mem_info}" | cut -d' ' -f1)
    local mem_usage=$(echo "${npu_mem_info}" | cut -d' ' -f2)
    
    log_info "  NPU 使用率: ${npu_usage}%"
    log_info "  内存使用率: ${mem_usage}%"
    
    local has_warning=0
    
    # 检查 RKLLM 是否在运行
    if [ "${rkllm_running}" -eq 0 ]; then
        log_llm "RKLLM 正在运行，将与 NPU 推理共享资源"
        
        # 如果 RKLLM 正在加载模型 (状态非 ready)
        local response=$(curl -s --connect-timeout 2 --max-time 3 "${LLM_HEALTH_URL}" 2>/dev/null || true)
        if echo "${response}" | grep -q '"status":"loading"'; then
            log_warn "RKLLM 正在加载模型，NPU 资源可能不足"
            log_warn "建议等待 RKLLM 就绪后再启动 NPU 推理"
            has_warning=1
        fi
    fi
    
    # 检查内存压力
    if [ "${mem_usage}" -gt "${MAX_MEMORY_USAGE_WARN}" ]; then
        log_warn "内存使用率过高 (${mem_usage}%)，可能影响 NPU 服务启动"
        has_warning=1
    fi
    
    return ${has_warning}
}

# ========== 启动 ==========
do_start() {
    echo ""
    echo "============================================"
    echo "  启动 RK3576 NPU 推理服务"
    echo "  (与 RKLLM 共存模式)"
    echo "============================================"
    echo ""
    
    # 1. 检查资源冲突
    check_npu_conflict
    local conflict=$?
    
    if [ "${conflict}" -eq 1 ]; then
        log_warn "检测到资源压力，建议稍后启动"
        echo ""
        echo "  如需强制启动，运行:"
        echo "    NPU_FORCE=1 $0 start"
        echo ""
        
        if [ "${NPU_FORCE}" != "1" ]; then
            read -p "  是否继续启动? (y/N): " choice
            if [ "${choice}" != "y" ] && [ "${choice}" != "Y" ]; then
                log_info "已取消启动"
                return 1
            fi
        fi
    fi
    
    # 2. 等待 RKLLM 就绪 (如果正在加载)
    local rkllm_ready=0
    local max_wait_iterations=$((LLM_WAIT_TIMEOUT / 3))
    for i in $(seq 1 ${max_wait_iterations}); do
        local response=$(curl -s --connect-timeout 2 --max-time 3 "${LLM_HEALTH_URL}" 2>/dev/null || true)
        if echo "${response}" | grep -q '"status":"ready"'; then
            rkllm_ready=1
            break
        elif echo "${response}" | grep -q '"status":"loading"'; then
            log_llm "RKLLM 加载中... (${i}/${max_wait_iterations})"
            sleep 3
        else
            break
        fi
    done
    
    if [ "${rkllm_ready}" -eq 1 ]; then
        log_llm "RKLLM 已就绪，可以安全启动 NPU 推理"
    else
        log_warn "RKLLM 未就绪，NPU 推理服务将在 RKLLM 就绪后自动恢复"
    fi
    
    # 3. 检查 NPU 模型
    log_info "检查 NPU 模型..."
    if [ ! -f "${MODEL_FILE}" ]; then
        log_err "RKNN 模型不存在: ${MODEL_FILE}"
        return 1
    fi
    local model_size=$(du -h "${MODEL_FILE}" 2>/dev/null | cut -f1 || echo 'unknown')
    log_ok "模型文件: ${MODEL_FILE} (${model_size})"
    
    # 4. 检查 Python 依赖
    log_info "检查依赖..."
    if ! python3 -c "import numpy" 2>/dev/null; then
        log_err "numpy 未安装"
        return 1
    fi
    if ! python3 -c "from rknnlite.api import RKNNLite" 2>/dev/null; then
        log_err "rknn-toolkit-lite2 未安装"
        return 1
    fi
    log_ok "依赖检查通过"
    
    # 5. 清理残留进程
    log_info "清理残留进程..."
    if [ -f "${PID_FILE}" ]; then
        local old_pid=$(cat "${PID_FILE}" 2>/dev/null || echo "")
        if [ -n "${old_pid}" ] && kill -0 "${old_pid}" 2>/dev/null; then
            kill "${old_pid}" 2>/dev/null || true
            sleep 1
            kill -9 "${old_pid}" 2>/dev/null || true
            log_warn "已终止旧进程: ${old_pid}"
        fi
        rm -f "${PID_FILE}"
    fi
    
    # 清理占用端口的进程
    local port_pid=$(netstat -tlnp 2>/dev/null | grep ":${SERVICE_PORT} " | grep LISTEN | awk '{print $NF}' | cut -d'/' -f1 || true)
    if [ -n "${port_pid}" ]; then
        local port_cmd=$(ps -p "${port_pid}" -o args= 2>/dev/null || echo "")
        if echo "${port_cmd}" | grep -q "python"; then
            kill -9 "${port_pid}" 2>/dev/null || true
            sleep 1
            log_warn "已释放端口 ${SERVICE_PORT}"
        fi
    fi
    
    # 6. 创建日志目录
    mkdir -p "${LOG_DIR}"
    
    # 7. 启动服务 (与 RKLLM 共存，核心隔离)
    log_info "启动 NPU 推理服务 (核心绑定: ${NPU_CORE_MASK})..."
    cd "${APP_DIR}"
    
    local log_file="${LOG_DIR}/npu_ai_server_$(date +%Y%m%d_%H%M%S).log"
    
    # 使用独立进程，绑定指定 NPU 核心避免与 RKLLM 冲突
    PYTHONUNBUFFERED=1 nohup python3 -u "app/${SERVICE_NAME}" \
        --model "${MODEL_FILE}" \
        --host "${SERVICE_HOST}" \
        --port "${SERVICE_PORT}" \
        --core-mask "${NPU_CORE_MASK}" \
        > "${log_file}" 2>&1 &
    
    local pid=$!
    echo "${pid}" > "${PID_FILE}"
    
    # 8. 等待并验证
    sleep 3
    
    if kill -0 "${pid}" 2>/dev/null; then
        log_ok "NPU 推理服务启动成功"
        echo ""
        echo "  PID:      ${pid}"
        echo "  地址:     ${SERVICE_HOST}:${SERVICE_PORT}"
        echo "  模型:     ${MODEL_FILE}"
        echo "  日志:     ${log_file}"
        echo ""
        
        # 检查与 RKLLM 的共存状态
        if [ "${rkllm_running}" -eq 0 ]; then
            log_info "RKLLM + NPU 推理服务均已启动"
        fi
        
        echo "  查看日志: $0 logs"
        echo "  停止服务: $0 stop"
        echo "  查看状态: $0 status"
        echo "  资源检查: $0 check-all"
    else
        log_err "NPU 推理服务启动失败"
        echo ""
        echo "  最近日志:"
        tail -30 "${log_file}" 2>/dev/null || echo "  (无日志)"
        rm -f "${PID_FILE}"
        return 1
    fi
}

# ========== 停止 ==========
do_stop() {
    echo ""
    log_info "停止 NPU 推理服务..."
    
    if [ -f "${PID_FILE}" ]; then
        local pid=$(cat "${PID_FILE}" 2>/dev/null || echo "")
        if [ -n "${pid}" ] && kill -0 "${pid}" 2>/dev/null; then
            log_info "终止进程: ${pid}"
            kill "${pid}" 2>/dev/null || true
            sleep 2
            kill -9 "${pid}" 2>/dev/null || true
            log_ok "进程已停止"
        else
            log_warn "进程 ${pid} 已不存在"
        fi
        rm -f "${PID_FILE}"
    else
        # 尝试按名称清理
        local pids=$(pgrep -f "python3.*${SERVICE_NAME}" 2>/dev/null || true)
        if [ -n "${pids}" ]; then
            log_warn "发现残留进程: ${pids}"
            pkill -9 -f "python3.*${SERVICE_NAME}" 2>/dev/null || true
            sleep 1
            log_ok "已清理"
        else
            log_info "无运行中的 NPU 服务"
        fi
    fi
}

# ========== 重启 ==========
do_restart() {
    do_stop
    sleep 2
    do_start
}

# ========== 状态 ==========
do_status() {
    echo ""
    echo "============================================"
    echo "  RK3576 NPU 推理服务状态"
    echo "============================================"
    echo ""
    
    # 系统信息
    echo "系统信息:"
    echo "  时间:     $(date '+%Y-%m-%d %H:%M:%S')"
    echo "  负载:     $(cat /proc/loadavg 2>/dev/null | cut -d' ' -f1-3 || echo 'unknown')"
    echo "  内存:     $(free -m 2>/dev/null | grep Mem | awk '{print $3"/"$2" MB"}' || echo 'unknown')"
    echo ""
    
    # NPU 信息
    echo "NPU 状态:"
    local npu_freq=$(cat /sys/devices/platform/27700000.npu/devfreq/27700000.npu/cur_freq 2>/dev/null || \
                     cat /sys/class/devfreq/fdab0000.npu/cur_freq 2>/dev/null || echo "N/A")
    echo "  NPU 频率: ${npu_freq} Hz"
    echo "  核心绑定: ${NPU_CORE_MASK}"
    if [ -f "/sys/kernel/debug/rknpu/load" ]; then
        local npu_load=$(cat /sys/kernel/debug/rknpu/load 2>/dev/null || echo "N/A")
        echo "  NPU 负载: ${npu_load}"
    fi
    echo ""
    
    # RKLLM 状态
    echo "RKLLM 服务:"
    check_rkllm_status
    echo ""
    
    # 模型文件
    echo "RKNN 模型:"
    if [ -f "${MODEL_FILE}" ]; then
        local size=$(du -h "${MODEL_FILE}" 2>/dev/null | cut -f1 || echo 'unknown')
        echo -e "  ${GREEN}[就绪]${RESET} ${MODEL_FILE} (${size})"
    else
        echo -e "  ${RED}[缺失]${RESET} ${MODEL_FILE}"
    fi
    echo ""
    
    # 服务状态
    echo "NPU 推理服务:"
    if [ -f "${PID_FILE}" ]; then
        local pid=$(cat "${PID_FILE}" 2>/dev/null || echo "")
        if [ -n "${pid}" ] && kill -0 "${pid}" 2>/dev/null; then
            local etime=$(ps -p "${pid}" -o etime= 2>/dev/null || echo "unknown")
            echo -e "  ${GREEN}[运行中]${RESET} PID=${pid}, 运行时长=${etime}"
        else
            echo -e "  ${YELLOW}[异常]${RESET} PID 文件存在但进程已退出"
        fi
    else
        echo -e "  ${RED}[未运行]${RESET}"
    fi
    
    # 端口状态
    echo ""
    echo "服务端口:"
    local port_info=$(netstat -tlnp 2>/dev/null | grep ":${SERVICE_PORT} " | grep LISTEN || true)
    if [ -n "${port_info}" ]; then
        echo -e "  ${GREEN}[监听中]${RESET} ${SERVICE_HOST}:${SERVICE_PORT}"
    else
        echo -e "  ${YELLOW}[未监听]${RESET} ${SERVICE_HOST}:${SERVICE_PORT}"
    fi
    
    # 最新日志
    echo ""
    echo "最新日志:"
    local latest_log=$(ls -t "${LOG_DIR}"/npu_ai_server_*.log 2>/dev/null | head -1 || true)
    if [ -n "${latest_log}" ]; then
        local log_lines=$(wc -l < "${latest_log}" 2>/dev/null || echo "0")
        echo "  文件: $(basename "${latest_log}") (${log_lines} 行)"
        echo "  最后5行:"
        tail -5 "${latest_log}" 2>/dev/null | sed 's/^/    /'
    else
        echo "  (无日志)"
    fi
}

# ========== 日志 ==========
do_logs() {
    local lines=${1:-50}
    local latest_log=$(ls -t "${LOG_DIR}"/npu_ai_server_*.log 2>/dev/null | head -1 || true)
    
    if [ -z "${latest_log}" ]; then
        log_err "未找到日志文件"
        return 1
    fi
    
    echo ""
    echo "查看 NPU 推理服务日志: $(basename "${latest_log}")"
    echo "最后 ${lines} 行:"
    echo "============================================"
    tail -n "${lines}" "${latest_log}"
    echo "============================================"
}

# ========== 测试 ==========
do_test() {
    echo ""
    echo "============================================"
    echo "  NPU 模型加载测试"
    echo "============================================"
    echo ""
    
    if [ ! -f "${MODEL_FILE}" ]; then
        log_err "RKNN 模型不存在: ${MODEL_FILE}"
        return 1
    fi
    
    # 先检查 RKLLM 是否在运行
    local rkllm_on=0
    check_rkllm_status && rkllm_on=1
    
    if [ "${rkllm_on}" -eq 1 ]; then
        log_warn "RKLLM 正在运行，测试将与 RKLLM 共享 NPU"
        log_info "将验证模型在共享环境下的加载和推理..."
    fi
    
    log_info "测试加载 RKNN 模型..."
    
    local result=$(python3 -c "
import sys
sys.path.insert(0, '${APP_DIR}')

try:
    from rknnlite.api import RKNNLite
    print('rknn-toolkit-lite2: OK')
except ImportError:
    print('rknn-toolkit-lite2: FAIL')
    sys.exit(1)

import numpy as np
import time

rknn = RKNNLite()

t0 = time.time()
ret = rknn.load_rknn('${MODEL_FILE}')
t1 = time.time()
print(f'load_rknn: ret={ret}, time={(t1-t0)*1000:.1f}ms')

if ret != 0:
    print('FAIL: load_rknn failed')
    rknn.release()
    sys.exit(1)

ret = rknn.init_runtime()
t2 = time.time()
print(f'init_runtime: ret={ret}, time={(t2-t1)*1000:.1f}ms')

if ret != 0:
    print('FAIL: init_runtime failed')
    rknn.release()
    sys.exit(1)

# 测试推理
test_input = np.random.randn(1, 3, 256).astype(np.float32)
outputs = rknn.inference(inputs=[test_input])
print(f'inference: shape={outputs[0].shape}')
print(f'output range: [{outputs[0].min():.4f}, {outputs[0].max():.4f}]')
print(f'predicted class: {np.argmax(outputs[0])}')

# 连续推理测试 (模拟实时推理)
times = []
for _ in range(100):
    t_start = time.perf_counter()
    rknn.inference(inputs=[test_input])
    t_end = time.perf_counter()
    times.append((t_end - t_start) * 1000)

times = np.array(times)
print(f'连续100次推理: 平均={np.mean(times):.2f}ms, P95={np.percentile(times, 95):.2f}ms')

rknn.release()
print('SUCCESS')
" 2>&1)
    
    echo "${result}"
    
    if echo "${result}" | grep -q "SUCCESS"; then
        log_ok "NPU 模型加载测试通过"
        if [ "${rkllm_on}" -eq 1 ]; then
            log_ok "与 RKLLM 共存测试通过"
        fi
    else
        log_err "NPU 模型加载测试失败"
        return 1
    fi
}

# ========== 性能测试 ==========
do_benchmark() {
    echo ""
    echo "============================================"
    echo "  NPU 推理性能测试"
    echo "============================================"
    echo ""
    
    if [ ! -f "${MODEL_FILE}" ]; then
        log_err "RKNN 模型不存在: ${MODEL_FILE}"
        return 1
    fi
    
    log_info "运行 NPU 推理性能测试..."
    log_info "将测试 1000 次推理的延迟分布..."
    
    python3 -c "
import numpy as np
import time
from rknnlite.api import RKNNLite

rknn = RKNNLite()
ret = rknn.load_rknn('${MODEL_FILE}')
if ret != 0:
    print(f'load_rknn failed: {ret}')
    exit(1)

ret = rknn.init_runtime()
if ret != 0:
    print(f'init_runtime failed: {ret}')
    exit(1)

test_input = np.random.randn(1, 3, 256).astype(np.float32)

# 预热
for _ in range(50):
    rknn.inference(inputs=[test_input])

# 批量测试
times = []
for _ in range(1000):
    t_start = time.perf_counter()
    rknn.inference(inputs=[test_input])
    t_end = time.perf_counter()
    times.append((t_end - t_start) * 1000)

times = np.array(times)
print(f'推理次数: 1000')
print(f'平均耗时: {np.mean(times):.3f} ms')
print(f'最小耗时: {np.min(times):.3f} ms')
print(f'最大耗时: {np.max(times):.3f} ms')
print(f'P50 耗时: {np.percentile(times, 50):.3f} ms')
print(f'P95 耗时: {np.percentile(times, 95):.3f} ms')
print(f'P99 耗时: {np.percentile(times, 99):.3f} ms')
print(f'吞吐量: {1000/np.mean(times):.1f} FPS')

rknn.release()
" 2>&1
    
    log_ok "性能测试完成"
}

# ========== 全服务检查 ==========
do_check_all() {
    echo ""
    echo "============================================"
    echo "  RK3576 NPU 服务完整检查"
    echo "============================================"
    echo ""
    
    # 系统资源
    echo "【系统资源】"
    echo "  CPU 负载: $(cat /proc/loadavg 2>/dev/null | cut -d' ' -f1-3)"
    echo "  内存使用: $(free -m 2>/dev/null | grep Mem | awk '{print $3"M/"$2"M"}')"
    echo "  磁盘空间: $(df -h / 2>/dev/null | tail -1 | awk '{print $5}')"
    echo ""
    
    # NPU 状态
    echo "【NPU 状态】"
    echo "  NPU 频率: $(cat /sys/class/devfreq/fdab0000.npu/cur_freq 2>/dev/null || echo 'N/A') Hz"
    if [ -f "/sys/kernel/debug/rknpu/load" ]; then
        echo "  NPU 负载: $(cat /sys/kernel/debug/rknpu/load 2>/dev/null || echo 'N/A')"
    fi
    echo ""
    
    # RKLLM 状态
    echo "【RKLLM 服务】"
    check_rkllm_status
    echo ""
    
    # NPU 推理服务
    echo "【NPU 推理服务】"
    if [ -f "${PID_FILE}" ]; then
        local pid=$(cat "${PID_FILE}" 2>/dev/null || echo "")
        if [ -n "${pid}" ] && kill -0 "${pid}" 2>/dev/null; then
            local etime=$(ps -p "${pid}" -o etime= 2>/dev/null || echo "unknown")
            echo -e "  ${GREEN}[运行中]${RESET} PID=${pid}, 运行时长=${etime}"
        else
            echo -e "  ${RED}[已停止]${RESET}"
        fi
    else
        echo -e "  ${RED}[未运行]${RESET}"
    fi
    
    # 端口检查
    echo ""
    echo "【端口状态】"
    echo "  AI 服务 (${SERVICE_PORT}): $(netstat -tlnp 2>/dev/null | grep ":${SERVICE_PORT} " | grep LISTEN | head -1 || echo '未监听')"
    echo "  RKLLM (${LLM_PORT}): $(netstat -tlnp 2>/dev/null | grep ":${LLM_PORT} " | grep LISTEN | head -1 || echo '未监听')"
    
    # 模型检查
    echo ""
    echo "【模型文件】"
    ls -lh "${MODEL_DIR}"/*.rknn 2>/dev/null | awk '{print "  " $NF " (" $5 ")"}' || echo "  无 RKNN 模型"
    
    echo ""
    log_ok "检查完成"
}

# ========== 帮助 ==========
usage() {
    echo ""
    echo "RK3576 NPU 推理服务管理 (与 RKLLM 共存版)"
    echo "============================================"
    echo ""
    echo "用法: $0 [命令] [参数]"
    echo ""
    echo "命令:"
    echo "  start       启动 NPU 推理服务 (检查与 RKLLM 的资源冲突)"
    echo "  stop        停止 NPU 推理服务"
    echo "  restart     重启 NPU 推理服务"
    echo "  status      查看服务状态"
    echo "  logs [N]    查看日志 (默认50行)"
    echo "  test        测试 NPU 模型加载 (含 RKLLM 共存测试)"
    echo "  benchmark   NPU 推理性能测试 (1000次)"
    echo "  check-all   检查所有 NPU 服务状态"
    echo "  help        显示此帮助"
    echo ""
    echo "环境变量:"
    echo "  NPU_FORCE=1       强制启动 (忽略资源警告)"
    echo "  NPU_CORE_MASK=xxx NPU 核心绑定 (auto/core0/core1/core0_1)"
    echo ""
    echo "核心绑定说明 (RK3576 与 RKLLM 共存):"
    echo "  auto          自动选择 (可能与 RKLLM 冲突)"
    echo "  core0         绑定核心 0 (RKLLM 可能使用)"
    echo "  core1         绑定核心 1 (推荐，与 RKLLM 隔离)"
    echo "  core0_1       绑定核心 0+1 (最大性能)"
    echo ""
    echo "示例:"
    echo "  $0 start                          # 启动服务 (核心绑定 core1)"
    echo "  NPU_CORE_MASK=core0 $0 start      # 绑定核心 0"
    echo "  NPU_CORE_MASK=auto $0 start       # 自动模式"
    echo "  NPU_FORCE=1 $0 start              # 强制启动"
    echo "  $0 check-all                      # 完整状态检查"
    echo "  $0 test                           # 模型加载测试"
    echo "  $0 benchmark                      # 性能测试"
    echo ""
}

# ========== 主入口 ==========
case "${1:-}" in
    start)
        do_start
        ;;
    stop)
        do_stop
        ;;
    restart)
        do_restart
        ;;
    status)
        do_status
        ;;
    logs)
        do_logs "${2:-50}"
        ;;
    test)
        do_test
        ;;
    benchmark)
        do_benchmark
        ;;
    check-all|check)
        do_check_all
        ;;
    help|--help|-h|"")
        usage
        ;;
    *)
        echo "未知命令: $1"
        usage
        exit 1
        ;;
esac

exit $?