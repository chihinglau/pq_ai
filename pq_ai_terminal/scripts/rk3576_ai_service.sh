#!/bin/bash
#
# RK3576 AI 推理服务管理脚本
# =========================
# 功能:
#   1. 检测并清理残留进程
#   2. 管理 RKLLM 大模型服务 (通过 systemctl)
#   3. 启动/停止/重启 AI 推理服务
#   4. 查看服务状态和日志
#
# 使用方法:
#   ./rk3576_ai_service.sh start          # 启动全部服务 (RKLLM + AI 推理)
#   ./rk3576_ai_service.sh stop           # 停止全部服务
#   ./rk3576_ai_service.sh restart        # 重启全部服务
#   ./rk3576_ai_service.sh status         # 查看全部服务状态
#   ./rk3576_ai_service.sh logs           # 查看 AI 推理服务日志
#   ./rk3576_ai_service.sh clean          # 强制清理所有残留进程
#   ./rk3576_ai_service.sh llm-start      # 仅启动 RKLLM 服务
#   ./rk3576_ai_service.sh llm-stop       # 仅停止 RKLLM 服务
#   ./rk3576_ai_service.sh llm-status     # 查看 RKLLM 服务状态
#

set -e

# ========== 配置 ==========
APP_NAME="PQ_AI_Service"
PROCESS_PATTERN="wave_inference_server"
SCRIPT_NAME="wave_inference_server_v5_npu.py"

# 路径配置
APP_DIR="/home/cat/pq_ai_v3"
LOG_DIR="${APP_DIR}/logs"
PID_FILE="${LOG_DIR}/ai_service.pid"
MODEL_DIR="${APP_DIR}/models"
DATA_DIR="/home/cat/pq_data"

# AI 推理服务配置
SERVICE_PORT=9090
SERVICE_HOST="192.168.100.1"
LOG_LEVEL="INFO"

# ========== RKLLM 服务配置 ==========
# RKLLM 服务端口 (HTTP API)
LLM_PORT=8080
LLM_HOST="127.0.0.1"

# RKLLM 模型配置
LLM_MODEL_FILE="/home/cat/ai/models/qwen3-1.7b-rk3576.rkllm"
LLAMA_CLI="/home/cat/ai/rk-llama.cpp-rknpu2/build_rk3576/bin/llama-cli"

# RKLLM systemd 服务名 (参考 RKLLM_Deployment_Guide.md)
LLM_SERVICE_NAME="rkllm-server"
MQTT_SERVICE_NAME="mosquitto"
MQTT_BRIDGE_SERVICE_NAME="rkllm-mqtt-bridge"

# RKLLM 部署目录
LLM_DEPLOY_DIR="/home/cat/rkllm-server-deploy"
LLM_LOG_DIR="${LLM_DEPLOY_DIR}/logs"

# 环境变量 (传递给 AI 推理服务)
export PYTHONUNBUFFERED=1
export PQ_MODEL_DIR="${MODEL_DIR}"
export PQ_LLM_API_URL="http://${LLM_HOST}:${LLM_PORT}/v1/chat/completions"
export PQ_LLM_MODEL="qwen3-1.7b-rk3576.rkllm"

# 颜色输出
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

# ========== RKLLM 服务管理 ==========

# 检查 RKLLM 服务状态 (通过 systemctl)
check_llm_systemctl() {
    if systemctl is-active --quiet "${LLM_SERVICE_NAME}" 2>/dev/null; then
        return 0  # 运行中
    else
        return 1  # 未运行
    fi
}

# 检查 RKLLM HTTP 服务状态
check_llm_http() {
    local url="http://${LLM_HOST}:${LLM_PORT}/health"
    local max_retries=3
    local retry=0
    
    while [ $retry -lt $max_retries ]; do
        local response=$(curl -s --connect-timeout 3 --max-time 5 "${url}" 2>/dev/null || true)
        if echo "${response}" | grep -q '"status"'; then
            local status=$(echo "${response}" | grep -o '"status":"[^"]*"' | cut -d'"' -f4)
            if [ "${status}" = "ready" ]; then
                return 0  # ready
            else
                return 2  # 非 ready 状态 (如 loading)
            fi
        fi
        retry=$((retry + 1))
        sleep 1
    done
    return 1  # 无法连接
}

# 启动 RKLLM 服务 (参考 RKLLM_Deployment_Guide.md 第 4.1 节)
start_llm_service() {
    echo ""
    echo "============================================"
    echo "  启动 RKLLM 大模型服务"
    echo "============================================"
    echo ""
    
    # 1. 检查模型文件
    log_llm "检查 RKLLM 模型..."
    if [ ! -f "${LLM_MODEL_FILE}" ]; then
        log_err "RKLLM 模型文件不存在: ${LLM_MODEL_FILE}"
        log_err "请确认模型已部署到正确位置"
        return 1
    fi
    log_ok "模型文件存在: ${LLM_MODEL_FILE}"
    
    # 2. 检查 llama-cli
    log_llm "检查 llama-cli..."
    if [ ! -f "${LLAMA_CLI}" ]; then
        log_err "llama-cli 不存在: ${LLAMA_CLI}"
        return 1
    fi
    log_ok "llama-cli 存在: ${LLAMA_CLI}"
    
    # 3. 启动 mosquitto (MQTT Broker)
    log_llm "启动 MQTT Broker (mosquitto)..."
    if command -v systemctl &> /dev/null; then
        sudo systemctl start "${MQTT_SERVICE_NAME}" 2>/dev/null || true
        sleep 1
        if systemctl is-active --quiet "${MQTT_SERVICE_NAME}" 2>/dev/null; then
            log_ok "mosquitto 已启动"
        else
            log_warn "mosquitto 启动可能失败，继续尝试..."
        fi
    fi
    
    # 4. 启动 RKLLM 服务 (通过 systemctl)
    log_llm "启动 RKLLM 服务 (${LLM_SERVICE_NAME})..."
    if command -v systemctl &> /dev/null; then
        sudo systemctl start "${LLM_SERVICE_NAME}" 2>/dev/null || true
        sleep 3
        
        # 检查 systemctl 状态
        if check_llm_systemctl; then
            log_ok "RKLLM 服务已启动 (systemctl: active)"
        else
            log_warn "RKLLM 服务 systemctl 状态: inactive，可能正在加载模型..."
        fi
        
        # 检查 HTTP 健康状态
        local http_status=0
        for i in {1..10}; do
            http_status=0
            check_llm_http || http_status=$?
            if [ $http_status -eq 0 ]; then
                log_ok "RKLLM 服务就绪 (HTTP: ready)"
                break
            elif [ $http_status -eq 2 ]; then
                log_llm "RKLLM 服务加载中... (${i}/10)"
                sleep 3
            else
                log_llm "等待 RKLLM 服务启动... (${i}/10)"
                sleep 3
            fi
        done
        
        if [ $http_status -ne 0 ]; then
            log_warn "RKLLM 服务可能未完全就绪，请检查日志:"
            log_warn "  journalctl -u ${LLM_SERVICE_NAME} -e --no-pager | tail -20"
        fi
    else
        # 无 systemctl，使用手动启动方式
        log_warn "未找到 systemctl，使用手动启动方式..."
        
        # 清理可能残留的 RKLLM 进程
        pkill -f "rkllm_server_v4" 2>/dev/null || true
        sleep 1
        
        # 手动启动 Flask 服务 (参考 RKLLM_Deployment_Guide.md 第 13.3.3 节)
        mkdir -p "${LLM_LOG_DIR}"
        local llm_log="${LLM_LOG_DIR}/service_$(date +%Y%m%d_%H%M%S).log"
        
        nohup python3 "${LLM_DEPLOY_DIR}/src/rkllm_server_v4.py" \
            --model "${LLM_MODEL_FILE}" \
            --llama-bin "${LLAMA_CLI}" \
            --host "${LLM_HOST}" \
            --port "${LLM_PORT}" \
            --threads 4 \
            --max-tokens 256 \
            > "${llm_log}" 2>&1 &
        
        local llm_pid=$!
        echo "${llm_pid}" > "${LOG_DIR}/llm_service.pid"
        
        # 等待并验证
        log_llm "等待 RKLLM 服务就绪 (PID: ${llm_pid})..."
        local http_status=0
        for i in {1..15}; do
            sleep 3
            if ! kill -0 "${llm_pid}" 2>/dev/null; then
                log_err "RKLLM 服务进程已退出，请检查日志: ${llm_log}"
                tail -20 "${llm_log}"
                return 1
            fi
            
            http_status=0
            check_llm_http || http_status=$?
            if [ $http_status -eq 0 ]; then
                log_ok "RKLLM 服务就绪 (HTTP: ready)"
                break
            elif [ $http_status -eq 2 ]; then
                log_llm "RKLLM 服务加载中... (${i}/15)"
            else
                log_llm "等待 RKLLM 服务启动... (${i}/15)"
            fi
        done
        
        if [ $http_status -ne 0 ]; then
            log_warn "RKLLM 服务可能未完全就绪"
            log_warn "查看日志: tail -f ${llm_log}"
        fi
    fi
    
    # 5. 启动 MQTT Bridge (可选)
    log_llm "启动 MQTT Bridge (${MQTT_BRIDGE_SERVICE_NAME})..."
    if command -v systemctl &> /dev/null; then
        sudo systemctl start "${MQTT_BRIDGE_SERVICE_NAME}" 2>/dev/null || true
        sleep 1
    fi
    
    echo ""
    log_ok "RKLLM 大模型服务启动流程完成"
    echo "  HTTP API: http://${LLM_HOST}:${LLM_PORT}"
    echo "  模型: ${LLM_MODEL_FILE}"
}

# 停止 RKLLM 服务
stop_llm_service() {
    echo ""
    log_llm "停止 RKLLM 大模型服务..."
    
    if command -v systemctl &> /dev/null; then
        # 停止 MQTT Bridge (反向顺序)
        sudo systemctl stop "${MQTT_BRIDGE_SERVICE_NAME}" 2>/dev/null || true
        sleep 1
        
        # 停止 RKLLM 服务
        sudo systemctl stop "${LLM_SERVICE_NAME}" 2>/dev/null || true
        sleep 2
        
        # 停止 mosquitto (可选，可能有其他服务在使用)
        # sudo systemctl stop "${MQTT_SERVICE_NAME}" 2>/dev/null || true
        
        log_ok "RKLLM 服务已停止 (systemctl)"
    else
        # 手动停止
        local llm_pid_file="${LOG_DIR}/llm_service.pid"
        if [ -f "${llm_pid_file}" ]; then
            local pid=$(cat "${llm_pid_file}" 2>/dev/null || echo "")
            if [ -n "${pid}" ] && kill -0 "${pid}" 2>/dev/null; then
                log_llm "终止 RKLLM 进程: ${pid}"
                kill "${pid}" 2>/dev/null || true
                sleep 1
                kill -9 "${pid}" 2>/dev/null || true
            fi
            rm -f "${llm_pid_file}"
        fi
        
        # 清理所有 RKLLM 相关进程
        pkill -f "rkllm_server_v4" 2>/dev/null || true
        pkill -f "llama-cli" 2>/dev/null || true
        sleep 1
        
        log_ok "RKLLM 服务已停止"
    fi
}

# 重启 RKLLM 服务
restart_llm_service() {
    stop_llm_service
    sleep 2
    start_llm_service
}

# 查看 RKLLM 服务状态
status_llm_service() {
    echo ""
    echo "============================================"
    echo "  RKLLM 大模型服务状态"
    echo "============================================"
    echo ""
    
    echo "服务状态 (systemctl):"
    if command -v systemctl &> /dev/null; then
        echo "  mosquitto:         $(systemctl is-active "${MQTT_SERVICE_NAME}" 2>/dev/null || echo 'unknown')"
        echo "  rkllm-server:      $(systemctl is-active "${LLM_SERVICE_NAME}" 2>/dev/null || echo 'unknown')"
        echo "  rkllm-mqtt-bridge: $(systemctl is-active "${MQTT_BRIDGE_SERVICE_NAME}" 2>/dev/null || echo 'unknown')"
    else
        echo "  (systemctl 不可用)"
    fi
    
    echo ""
    echo "HTTP 健康检查:"
    if [ -f "${LLM_MODEL_FILE}" ]; then
        local response=$(curl -s --connect-timeout 3 --max-time 5 "http://${LLM_HOST}:${LLM_PORT}/health" 2>/dev/null || true)
        if [ -n "${response}" ]; then
            local status=$(echo "${response}" | grep -o '"status":"[^"]*"' | cut -d'"' -f4 || echo 'unknown')
            local model=$(echo "${response}" | grep -o '"model":"[^"]*"' | cut -d'"' -f4 || echo 'unknown')
            local uptime=$(echo "${response}" | grep -o '"uptime":"[^"]*"' | cut -d'"' -f4 || echo 'unknown')
            
            if [ "${status}" = "ready" ]; then
                echo -e "  ${GREEN}✓ 就绪${RESET}"
            else
                echo -e "  ${YELLOW}◌ ${status}${RESET}"
            fi
            echo "  模型: ${model}"
            echo "  运行时间: ${uptime}"
        else
            echo -e "  ${RED}✗ 无法连接${RESET}"
            echo "  HTTP API: http://${LLM_HOST}:${LLM_PORT}"
        fi
    else
        echo -e "  ${RED}✗ 模型文件不存在${RESET}"
        echo "  预期位置: ${LLM_MODEL_FILE}"
    fi
    
    echo ""
    echo "资源使用:"
    if [ -f "${LLM_MODEL_FILE}" ]; then
        local model_size=$(du -h "${LLM_MODEL_FILE}" 2>/dev/null | cut -f1 || echo 'unknown')
        echo "  模型大小: ${model_size}"
    fi
    echo "  NPU 频率: $(cat /sys/class/devfreq/fdab0000.npu/cur_freq 2>/dev/null || echo 'unknown') Hz"
}

# ========== 进程检测 ==========

# 检测残留进程
detect_ai_processes() {
    echo ""
    log_info "扫描 AI 推理服务进程..."
    
    local processes=$(pgrep -f "${PROCESS_PATTERN}" 2>/dev/null || true)
    
    if [ -n "${processes}" ]; then
        log_warn "发现残留进程:"
        echo "${processes}" | while read pid; do
            local cmd=$(ps -p "${pid}" -o args= 2>/dev/null || echo "unknown")
            echo "  PID: ${pid} - ${cmd}"
        done
        return 1
    else
        log_ok "无残留进程"
        return 0
    fi
}

# 检测端口占用
detect_port_usage() {
    local port=$1
    local service_name=$2
    log_info "检测端口 ${port} (${service_name}) 占用..."
    
    local port_pid=$(netstat -tlnp 2>/dev/null | grep ":${port} " | grep LISTEN | awk '{print $NF}' | cut -d'/' -f1 || true)
    
    if [ -n "${port_pid}" ]; then
        log_warn "端口 ${port} 被占用 (PID: ${port_pid})"
        local cmd=$(ps -p "${port_pid}" -o args= 2>/dev/null || echo "unknown")
        echo "  占用进程: ${cmd}"
        return 1
    else
        log_ok "端口 ${port} 空闲"
        return 0
    fi
}

# 清理 AI 推理服务残留进程
cleanup_ai_processes() {
    log_info "清理 AI 推理服务残留进程..."
    
    # 1. 清理 PID 文件中记录的进程
    if [ -f "${PID_FILE}" ]; then
        local pid=$(cat "${PID_FILE}" 2>/dev/null || echo "")
        if [ -n "${pid}" ] && kill -0 "${pid}" 2>/dev/null; then
            log_warn "终止 PID 文件记录的进程: ${pid}"
            kill "${pid}" 2>/dev/null || true
            sleep 1
            kill -9 "${pid}" 2>/dev/null || true
        fi
        rm -f "${PID_FILE}"
    fi
    
    # 2. 清理所有匹配的 python 进程
    local processes=$(pgrep -f "python3.*${SCRIPT_NAME}" 2>/dev/null || true)
    if [ -n "${processes}" ]; then
        log_warn "终止所有 ${SCRIPT_NAME} 相关进程..."
        pkill -f "python3.*${SCRIPT_NAME}" 2>/dev/null || true
        sleep 1
        pkill -9 -f "python3.*${SCRIPT_NAME}" 2>/dev/null || true
        sleep 0.5
    fi
    
    # 3. 清理占用服务端口的进程
    local port_pid=$(netstat -tlnp 2>/dev/null | grep ":${SERVICE_PORT} " | grep LISTEN | awk '{print $NF}' | cut -d'/' -f1 || true)
    if [ -n "${port_pid}" ]; then
        local cmd=$(ps -p "${port_pid}" -o args= 2>/dev/null || echo "")
        if echo "${cmd}" | grep -q "${SCRIPT_NAME}"; then
            log_warn "终止端口 ${SERVICE_PORT} 占用进程: ${port_pid}"
            kill -9 "${port_pid}" 2>/dev/null || true
        fi
    fi
    
    # 4. 验证清理结果
    local remaining=$(pgrep -f "${PROCESS_PATTERN}" 2>/dev/null | wc -l || echo "0")
    if [ "${remaining}" -gt 0 ]; then
        log_warn "仍有 ${remaining} 个残留进程，尝试使用 fuser 清理..."
        fuser -k "${SERVICE_PORT}/tcp" 2>/dev/null || true
        sleep 1
    fi
    
    # 5. 最终验证
    local final=$(pgrep -f "${PROCESS_PATTERN}" 2>/dev/null | wc -l || echo "0")
    if [ "${final}" -eq 0 ]; then
        log_ok "AI 推理服务残留进程已清理"
        return 0
    else
        log_err "清理失败，仍有 ${final} 个进程"
        return 1
    fi
}

# 清理全部服务残留进程 (AI + RKLLM)
cleanup_all() {
    echo ""
    echo "============================================"
    echo "  清理所有残留进程"
    echo "============================================"
    echo ""
    
    cleanup_ai_processes
    
    log_llm "检查 RKLLM 服务残留..."
    local llm_pid_file="${LOG_DIR}/llm_service.pid"
    if [ -f "${llm_pid_file}" ]; then
        local pid=$(cat "${llm_pid_file}" 2>/dev/null || echo "")
        if [ -n "${pid}" ] && kill -0 "${pid}" 2>/dev/null; then
            log_warn "终止 RKLLM 进程: ${pid}"
            kill -9 "${pid}" 2>/dev/null || true
        fi
        rm -f "${llm_pid_file}"
    fi
    
    # 清理可能的 RKLLM 手动进程
    local rkllm_procs=$(pgrep -f "rkllm_server_v4" 2>/dev/null || true)
    if [ -n "${rkllm_procs}" ]; then
        log_warn "终止残留的 RKLLM 服务进程..."
        pkill -9 -f "rkllm_server_v4" 2>/dev/null || true
    fi
    
    # 清理端口占用
    fuser -k "${SERVICE_PORT}/tcp" 2>/dev/null || true
    fuser -k "${LLM_PORT}/tcp" 2>/dev/null || true
    
    log_ok "清理完成"
}

# ========== AI 推理服务操作 ==========

# 启动 AI 推理服务
do_start_ai() {
    echo ""
    echo "============================================"
    echo "  启动 AI 推理服务"
    echo "============================================"
    echo ""
    
    # 1. 检测并清理残留
    if ! detect_ai_processes; then
        log_warn "检测到残留进程，自动清理..."
        cleanup_ai_processes
    fi
    
    # 2. 检测端口占用
    if ! detect_port_usage "${SERVICE_PORT}" "AI 推理服务"; then
        log_warn "端口被占用，尝试释放..."
        cleanup_ai_processes
        sleep 1
        if ! detect_port_usage "${SERVICE_PORT}" "AI 推理服务"; then
            log_err "端口仍被占用，请手动处理"
            return 1
        fi
    fi
    
    # 3. 检查目录
    mkdir -p "${LOG_DIR}" "${DATA_DIR}"
    
    # 4. 检查依赖
    log_info "检查 Python 依赖..."
    if ! python3 -c "import numpy" 2>/dev/null; then
        log_err "numpy 未安装"
        return 1
    fi
    
    # 5. 检查 RKLLM 服务 (AI 推理服务依赖 RKLLM)
    log_info "检查 RKLLM 服务..."
    if check_llm_http; then
        log_ok "RKLLM 服务运行中"
    else
        log_warn "RKLLM 服务未就绪，LLM 辅助决策功能将使用模拟响应"
        log_warn "如需完整 LLM 功能，请先运行: $0 llm-start"
    fi
    
    # 6. 检查 RKNN 模型
    log_info "检查 RKNN 模型..."
    local model_count=$(find "${MODEL_DIR}" -name "*.rknn" 2>/dev/null | wc -l || echo "0")
    if [ "${model_count}" -eq 0 ]; then
        log_warn "未找到 RKNN 模型文件 (将使用 CPU 回退)"
    else
        log_ok "找到 ${model_count} 个 RKNN 模型文件"
    fi
    
    # 7. 启动服务
    log_info "启动 AI 推理服务..."
    cd "${APP_DIR}"
    
    local log_file="${LOG_DIR}/ai_server_$(date +%Y%m%d_%H%M%S).log"
    
    nohup python3 -u "app/${SCRIPT_NAME}" \
        --model "${MODEL_DIR}/cnn1d_8class.rknn" \
        --host "${SERVICE_HOST}" \
        --port "${SERVICE_PORT}" \
        --log-level "${LOG_LEVEL}" \
        > "${log_file}" 2>&1 &
    
    local pid=$!
    echo "${pid}" > "${PID_FILE}"
    
    # 8. 等待并验证
    sleep 3
    
    if kill -0 "${pid}" 2>/dev/null; then
        log_ok "AI 推理服务启动成功"
        echo ""
        echo "  PID:     ${pid}"
        echo "  地址:    ${SERVICE_HOST}:${SERVICE_PORT}"
        echo "  日志:    ${log_file}"
        echo "  模型:    ${MODEL_DIR}"
        echo ""
        echo "  RKLLM API: ${PQ_LLM_API_URL}"
        echo "  RKLLM 模型: ${PQ_LLM_MODEL}"
        echo ""
        echo "  查看日志: tail -f ${log_file}"
        echo "  停止服务: $0 stop"
        echo "  查看状态: $0 status"
    else
        log_err "AI 推理服务启动失败"
        echo ""
        echo "  最近日志:"
        tail -20 "${log_file}" 2>/dev/null || echo "  (无日志)"
        return 1
    fi
}

# 启动全部服务 (RKLLM + AI 推理)
do_start() {
    echo ""
    echo "============================================"
    echo "  启动 RK3576 全部服务"
    echo "============================================"
    
    # 1. 先启动 RKLLM 服务
    start_llm_service
    
    # 2. 等待 RKLLM 服务就绪
    log_info "等待 RKLLM 服务完全就绪..."
    local wait_count=0
    while [ $wait_count -lt 15 ]; do
        if check_llm_http; then
            break
        fi
        wait_count=$((wait_count + 1))
        sleep 2
    done
    
    if [ $wait_count -ge 15 ]; then
        log_warn "RKLLM 服务启动超时，继续启动 AI 推理服务 (LLM 将使用模拟回退)"
    else
        log_ok "RKLLM 服务已就绪"
    fi
    
    # 2. 启动 AI 推理服务
    do_start_ai
}

# 停止全部服务
do_stop() {
    echo ""
    echo "============================================"
    echo "  停止 RK3576 全部服务"
    echo "============================================"
    echo ""
    
    # 1. 先停止 AI 推理服务
    cleanup_ai_processes
    
    # 2. 再停止 RKLLM 服务
    local stop_llm=${2:-"true"}
    if [ "${stop_llm}" != "no-llm" ]; then
        stop_llm_service
    fi
    
    log_ok "全部服务已停止"
}

# 重启全部服务
do_restart() {
    echo ""
    echo "============================================"
    echo "  重启 RK3576 全部服务"
    echo "============================================"
    echo ""
    
    # 先停止
    cleanup_ai_processes
    stop_llm_service
    sleep 2
    
    # 再启动
    do_start
}

# 查看全部服务状态
do_status() {
    echo ""
    echo "============================================"
    echo "  RK3576 服务状态总览"
    echo "============================================"
    echo ""
    
    # 系统信息
    echo "系统信息:"
    echo "  时间:     $(date '+%Y-%m-%d %H:%M:%S')"
    echo "  负载:     $(cat /proc/loadavg 2>/dev/null | cut -d' ' -f1-3 || echo 'unknown')"
    echo "  内存:     $(free -m 2>/dev/null | grep Mem | awk '{print $3"/"$2" MB"}' || echo 'unknown')"
    echo ""
    
    # RKLLM 服务状态
    status_llm_service
    
    # AI 推理服务状态
    echo ""
    echo "AI 推理服务状态:"
    local processes=$(pgrep -f "${PROCESS_PATTERN}" 2>/dev/null || true)
    if [ -n "${processes}" ]; then
        echo -e "  ${GREEN}[运行中]${RESET} ${APP_NAME}"
        echo "${processes}" | while read pid; do
            local cmd=$(ps -p "${pid}" -o args= 2>/dev/null || echo "unknown")
            local etime=$(ps -p "${pid}" -o etime= 2>/dev/null || echo "unknown")
            echo "    PID: ${pid}"
            echo "    命令: ${cmd}"
            echo "    运行时长: ${etime}"
        done
    else
        echo -e "  ${RED}[已停止]${RESET} ${APP_NAME}"
    fi
    
    # AI 服务端口状态
    echo ""
    echo "AI 服务端口:"
    local port_info=$(netstat -tlnp 2>/dev/null | grep ":${SERVICE_PORT} " | grep LISTEN || true)
    if [ -n "${port_info}" ]; then
        echo -e "  ${GREEN}[监听中]${RESET} ${SERVICE_HOST}:${SERVICE_PORT}"
    else
        echo -e "  ${YELLOW}[未监听]${RESET} ${SERVICE_HOST}:${SERVICE_PORT}"
    fi
    
    # AI 模型状态
    echo ""
    echo "RKNN 模型:"
    if [ -d "${MODEL_DIR}" ]; then
        local model_count=$(find "${MODEL_DIR}" -name "*.rknn" 2>/dev/null | wc -l || echo "0")
        echo "  数量: ${model_count}"
        if [ "${model_count}" -gt 0 ]; then
            ls -lh "${MODEL_DIR}"/*.rknn 2>/dev/null | awk '{print "    " $NF " (" $5 ")"}'
        else
            echo "  (无 RKNN 模型)"
        fi
    else
        echo "  模型目录不存在"
    fi
    
    # AI 日志状态
    echo ""
    echo "AI 日志:"
    local latest_log=$(ls -t "${LOG_DIR}"/ai_server_*.log 2>/dev/null | head -1 || true)
    if [ -n "${latest_log}" ]; then
        local log_size=$(du -h "${latest_log}" 2>/dev/null | cut -f1 || echo "unknown")
        local log_lines=$(wc -l < "${latest_log}" 2>/dev/null || echo "0")
        echo "  最新: $(basename "${latest_log}")"
        echo "  大小: ${log_size}"
        echo "  行数: ${log_lines}"
    else
        echo "  无日志文件"
    fi
}

# ========== 日志查看 ==========

# 查看 AI 推理服务日志
do_logs() {
    local lines=${1:-50}
    local latest_log=$(ls -t "${LOG_DIR}"/ai_server_*.log 2>/dev/null | head -1 || true)
    
    if [ -z "${latest_log}" ]; then
        log_err "未找到 AI 推理服务日志"
        return 1
    fi
    
    echo ""
    echo "查看 AI 推理服务日志: $(basename "${latest_log}")"
    echo "最后 ${lines} 行:"
    echo "============================================"
    tail -n "${lines}" "${latest_log}"
    echo "============================================"
}

# 查看 RKLLM 服务日志
do_llm_logs() {
    local lines=${1:-50}
    
    # 优先查看 systemd 日志
    if command -v systemctl &> /dev/null; then
        local journal_log=$(journalctl -u "${LLM_SERVICE_NAME}" --no-pager -n "${lines}" 2>/dev/null || true)
        if [ -n "${journal_log}" ]; then
            echo ""
            echo "查看 RKLLM 服务日志 (systemd journal):"
            echo "============================================"
            echo "${journal_log}"
            echo "============================================"
            return 0
        fi
    fi
    
    # 回退到应用日志
    local latest_log=$(ls -t "${LLM_LOG_DIR}"/service_*.log 2>/dev/null | head -1 || true)
    if [ -z "${latest_log}" ]; then
        log_err "未找到 RKLLM 服务日志"
        return 1
    fi
    
    echo ""
    echo "查看 RKLLM 服务日志: $(basename "${latest_log}")"
    echo "最后 ${lines} 行:"
    echo "============================================"
    tail -n "${lines}" "${latest_log}"
    echo "============================================"
}

# ========== 主入口 ==========

usage() {
    echo ""
    echo "RK3576 AI 推理服务管理 (v2.0)"
    echo "================================"
    echo ""
    echo "使用方法: $0 [命令] [参数]"
    echo ""
    echo "【全部服务管理】"
    echo "  start             启动全部服务 (RKLLM + AI 推理)"
    echo "  stop              停止全部服务"
    echo "  restart           重启全部服务"
    echo "  status            查看全部服务状态"
    echo "  logs              查看 AI 推理服务日志 (默认50行)"
    echo "  clean             强制清理所有残留进程"
    echo ""
    echo "【RKLLM 大模型服务管理】"
    echo "  llm-start         启动 RKLLM 服务"
    echo "  llm-stop          停止 RKLLM 服务"
    echo "  llm-restart       重启 RKLLM 服务"
    echo "  llm-status        查看 RKLLM 服务状态"
    echo "  llm-logs          查看 RKLLM 服务日志"
    echo ""
    echo "【AI 推理服务管理】"
    echo "  ai-start          启动 AI 推理服务 (依赖 RKLLM)"
    echo "  ai-stop           停止 AI 推理服务"
    echo "  ai-restart        重启 AI 推理服务"
    echo "  ai-status         查看 AI 推理服务状态"
    echo ""
    echo "示例:"
    echo "  $0 start              # 启动全部服务"
    echo "  $0 llm-start          # 仅启动 RKLLM"
    echo "  $0 ai-start           # 仅启动 AI 推理"
    echo "  $0 logs 100           # 查看最近100行 AI 日志"
    echo "  $0 llm-logs 50        # 查看最近50行 RKLLM 日志"
    echo "  $0 status             # 查看全部状态"
    echo ""
}

case "${1:-}" in
    # 全部服务管理
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
    clean)
        cleanup_all
        ;;
    
    # RKLLM 服务管理
    llm-start)
        start_llm_service
        ;;
    llm-stop)
        stop_llm_service
        ;;
    llm-restart)
        restart_llm_service
        ;;
    llm-status)
        status_llm_service
        ;;
    llm-logs)
        do_llm_logs "${2:-50}"
        ;;
    
    # AI 推理服务管理 (使用 NPU)
    ai-start)
        cd "${APP_DIR}/scripts" && ./npu_ai_service.sh start
        ;;
    ai-stop)
        cd "${APP_DIR}/scripts" && ./npu_ai_service.sh stop
        ;;
    ai-restart)
        cd "${APP_DIR}/scripts" && ./npu_ai_service.sh restart
        ;;
    ai-status)
        cd "${APP_DIR}/scripts" && ./npu_ai_service.sh status
        ;;
    ai-logs)
        cd "${APP_DIR}/scripts" && ./npu_ai_service.sh logs "${2:-50}"
        ;;
    ai-test)
        cd "${APP_DIR}/scripts" && ./npu_ai_service.sh test
        ;;
    ai-benchmark)
        cd "${APP_DIR}/scripts" && ./npu_ai_service.sh benchmark
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
