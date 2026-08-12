#!/bin/bash
#
# 一键部署和运行完整测试
# 本脚本用于将wave_sender_arm部署到T536并运行完整测试
#
# 正确的业务流程:
#   T536采集原始波形 -> USB ECM发送 -> RK3576接收/特征提取/AI推理 -> 返回结果
#
# 使用方法: bash deploy_and_test.sh [--skip-compile]
#

set -e

# ========== 配置 ==========
T536_USER=csg
T536_HOST=192.168.14.101
T536_PORT=8888
T536_DEST_DIR=~/wave_sender_test

RK3576_USER=cat
RK3576_HOST=192.168.137.204
RK3576_PORT=22
RK3576_DEST_DIR=~/ai_inference

# USB ECM地址
T536_USB_IP=192.168.100.2
RK3576_USB_IP=192.168.100.1

# 项目路径
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PROJECT_DIR=$(dirname "$SCRIPT_DIR")
DEPLOY_DIR=${PROJECT_DIR}/deploy
INFERENCE_SCRIPT=${PROJECT_DIR}/app/wave_inference_server_v2.py
CROSS_COMPILE_SCRIPT=${PROJECT_DIR}/scripts/cross_compile_wave_sender.sh

# ========== 颜色定义 ==========
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

# ========== 日志函数 ==========
log_info() {
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo -e "${GREEN}[${timestamp}] [INFO]${NC} $1"
}

log_warn() {
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo -e "${YELLOW}[${timestamp}] [WARN]${NC} $1"
}

log_error() {
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo -e "${RED}[${timestamp}] [ERROR]${NC} $1"
}

log_step() {
    echo ""
    echo -e "${CYAN}============================================================${NC}"
    echo -e "${CYAN}  $1${NC}"
    echo -e "${CYAN}============================================================${NC}"
    echo ""
}

# ========== 参数解析 ==========
SKIP_COMPILE=false
for arg in "$@"; do
    case $arg in
        --skip-compile)
            SKIP_COMPILE=true
            ;;
        --help|-h)
            echo "Usage: $0 [--skip-compile]"
            echo "  --skip-compile  跳过交叉编译步骤（使用已编译的部署包）"
            echo "  --help          显示帮助信息"
            exit 0
            ;;
    esac
done

# ========== 主流程 ==========
clear
echo ""
echo -e "${BLUE}╔══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║     PQ AI Terminal - 一键部署和测试脚本                    ║${NC}"
echo -e "${BLUE}╚══════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "${CYAN}  正确业务流程:${NC}"
echo -e "  ┌─────────────┐    USB ECM     ┌─────────────┐"
echo -e "  │   T536      │  ──────────>  │   RK3576    │"
echo -e "  │  (采集端)   │  发送原始波形  │  (计算端)   │"
echo -e "  │             │  <──────────  │             │"
echo -e "  │             │  返回推理结果  │  特征提取   │"
echo -e "  │             │               │  AI推理     │"
echo -e "  └─────────────┘               └─────────────┘"
echo ""
echo -e "  ${GREEN}T536:${NC}    ${T536_USER}@${T536_HOST}:${T536_PORT}"
echo -e "  ${GREEN}RK3576:${NC}  ${RK3576_USER}@${RK3576_HOST}:${RK3576_PORT}"
echo -e "  ${GREEN}USB ECM:${NC} T536(${T536_USB_IP}) <-> RK3576(${RK3576_USB_IP})"
echo -e "  ${GREEN}推理服务:${NC} ${RK3576_USB_IP}:9090"
echo ""

# ========== 步骤0: 交叉编译（可选） ==========
if [ "$SKIP_COMPILE" = false ]; then
    log_step "步骤 0: 交叉编译 wave_sender_arm"
    
    if [ -f "$CROSS_COMPILE_SCRIPT" ]; then
        log_info "执行交叉编译脚本: $CROSS_COMPILE_SCRIPT"
        log_info "注意: 此脚本需在Ubuntu交叉编译服务器上运行"
        
        # 检查是否在交叉编译服务器上
        if [ -f /opt/scm/gcc-linaro-5.3.1-2016.05-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-gcc ]; then
            log_info "检测到交叉编译工具链，开始编译..."
            bash "$CROSS_COMPILE_SCRIPT"
            
            if [ -f "${DEPLOY_DIR}/wave_sender_arm_armhf.tar.gz" ]; then
                log_info "交叉编译成功: ${DEPLOY_DIR}/wave_sender_arm_armhf.tar.gz"
            else
                log_error "编译完成但未找到部署包！"
                exit 1
            fi
        else
            log_warn "当前环境不是交叉编译服务器"
            log_warn "请在Ubuntu服务器(192.168.72.128)上执行:"
            echo "    ssh liuzhixing@192.168.72.128"
            echo "    cd ${PROJECT_DIR}"
            echo "    bash scripts/cross_compile_wave_sender.sh"
            echo ""
            log_info "或者使用 --skip-compile 跳过编译，直接使用已有的部署包"
            exit 1
        fi
    else
        log_error "找不到交叉编译脚本: $CROSS_COMPILE_SCRIPT"
        exit 1
    fi
else
    log_info "跳过交叉编译步骤 (--skip-compile)"
fi

# ========== 步骤1: 检查部署包 ==========
log_step "步骤 1: 检查部署包"

DEPLOY_PKG="${DEPLOY_DIR}/wave_sender_arm_armhf.tar.gz"

if [ ! -f "$DEPLOY_PKG" ]; then
    log_error "找不到部署包: $DEPLOY_PKG"
    log_error "请先交叉编译: bash scripts/cross_compile_wave_sender.sh"
    exit 1
fi

PKG_SIZE=$(du -h "$DEPLOY_PKG" | cut -f1)
log_info "部署包: $DEPLOY_PKG (${PKG_SIZE})"
log_info "包内容:"
tar tzf "$DEPLOY_PKG" 2>/dev/null | while read -r line; do
    echo -e "    ${BLUE}${line}${NC}"
done

# ========== 步骤2: 上传到T536 ==========
log_step "步骤 2: 上传部署包到T536"

log_info "目标: ${T536_USER}@${T536_HOST}:${T536_PORT}"
log_info "请输入密码: Iot@csg123"
echo ""

log_info "创建目标目录 ${T536_DEST_DIR}..."
ssh -p ${T536_PORT} ${T536_USER}@${T536_HOST} "mkdir -p ${T536_DEST_DIR}"

log_info "上传部署包..."
scp -P ${T536_PORT} "$DEPLOY_PKG" \
    ${T536_USER}@${T536_HOST}:${T536_DEST_DIR}/

log_info "解压部署包..."
ssh -p ${T536_PORT} ${T536_USER}@${T536_HOST} \
    "cd ${T536_DEST_DIR} && tar xzf wave_sender_arm_armhf.tar.gz && chmod +x wave_sender_arm run.sh"

log_info "验证部署包..."
ssh -p ${T536_PORT} ${T536_USER}@${T536_HOST} \
    "ls -la ${T536_DEST_DIR}/ && echo '---' && ls -la ${T536_DEST_DIR}/lib/ 2>/dev/null || echo '(lib目录不存在)'"

log_info "T536部署完成"

# ========== 步骤3: 上传AI推理服务到RK3576 ==========
log_step "步骤 3: 上传AI推理服务到RK3576"

log_info "目标: ${RK3576_USER}@${RK3576_HOST}:${RK3576_PORT}"
log_info "请输入密码: 123456"
echo ""

if [ ! -f "$INFERENCE_SCRIPT" ]; then
    log_error "找不到推理服务脚本: $INFERENCE_SCRIPT"
    exit 1
fi

log_info "创建目标目录 ${RK3576_DEST_DIR}..."
ssh -p ${RK3576_PORT} ${RK3576_USER}@${RK3576_HOST} "mkdir -p ${RK3576_DEST_DIR}"

log_info "上传推理服务脚本..."
scp -P ${RK3576_PORT} "$INFERENCE_SCRIPT" \
    ${RK3576_USER}@${RK3576_HOST}:${RK3576_DEST_DIR}/wave_inference_server.py

log_info "验证上传..."
ssh -p ${RK3576_PORT} ${RK3576_USER}@${RK3576_HOST} \
    "ls -la ${RK3576_DEST_DIR}/wave_inference_server.py && wc -l ${RK3576_DEST_DIR}/wave_inference_server.py"

log_info "RK3576上传完成"

# ========== 步骤4: 在RK3576上启动AI推理服务 ==========
log_step "步骤 4: 在RK3576上启动AI推理服务"

log_info "清理旧的服务进程..."
ssh -p ${RK3576_PORT} ${RK3576_USER}@${RK3576_HOST} \
    "pkill -9 -f wave_inference_server 2>/dev/null || true"

sleep 1

log_info "启动AI推理服务..."
log_info "监听地址: ${RK3576_USB_IP}:9090"
log_info "日志文件: ${RK3576_DEST_DIR}/ai_server.log"

ssh -p ${RK3576_PORT} ${RK3576_USER}@${RK3576_HOST} \
    "cd ${RK3576_DEST_DIR} && PYTHONUNBUFFERED=1 nohup python3 -u wave_inference_server.py --host ${RK3576_USB_IP} --port 9090 > ai_server.log 2>&1 &"

sleep 3

log_info "检查服务状态..."
SERVICE_RUNNING=$(ssh -p ${RK3576_PORT} ${RK3576_USER}@${RK3576_HOST} \
    "ps aux | grep wave_inference_server | grep -v grep | wc -l")

if [ "$SERVICE_RUNNING" -gt 0 ]; then
    log_info "✅ AI推理服务已启动 (${SERVICE_RUNNING}个进程)"
else
    log_warn "⚠️  AI服务可能未启动，检查日志:"
    ssh -p ${RK3576_PORT} ${RK3576_USER}@${RK3576_HOST} \
        "tail -20 ${RK3576_DEST_DIR}/ai_server.log"
fi

log_info "查看服务启动日志..."
ssh -p ${RK3576_PORT} ${RK3576_USER}@${RK3576_HOST} \
    "head -30 ${RK3576_DEST_DIR}/ai_server.log" 2>/dev/null || echo "(日志文件尚未生成)"

# ========== 步骤5: 在T536上运行测试 ==========
log_step "步骤 5: 在T536上运行测试"

log_info "运行 wave_sender_arm 参数:"
log_info "  采集周期: 5"
log_info "  发送地址: ${RK3576_USB_IP}:9090 (RK3576 usb0)"
log_info "  日志文件: wave_sender.log"
log_info "  运行方式: /lib32/ld-linux-armhf.so.3 (32位动态链接器)"
echo ""

log_info "开始运行测试 (等待结果)..."
echo ""

# 在T536上运行测试 (timeout 60秒)
ssh -p ${T536_PORT} ${T536_USER}@${T536_HOST} \
    "cd ${T536_DEST_DIR} && timeout 60 ./run.sh --cycles 5 --server ${RK3576_USB_IP} --port 9090 --log wave_sender.log --debug" 2>&1

TEST_EXIT=$?

echo ""
if [ $TEST_EXIT -eq 0 ]; then
    log_info "✅ 测试成功完成"
elif [ $TEST_EXIT -eq 124 ]; then
    log_warn "⚠️  测试超时 (60秒)，可能仍在运行中"
else
    log_error "❌ 测试失败 (退出码: $TEST_EXIT)"
fi

# ========== 步骤6: 收集日志 ==========
log_step "步骤 6: 收集并显示日志"

TIMESTAMP=$(date '+%Y%m%d_%H%M%S')
LOG_DIR=${PROJECT_DIR}/test_results/${TIMESTAMP}
mkdir -p "$LOG_DIR"

log_info "从T536下载wave_sender.log..."
scp -P ${T536_PORT} ${T536_USER}@${T536_HOST}:${T536_DEST_DIR}/wave_sender.log \
    "${LOG_DIR}/wave_sender_t536.log" 2>/dev/null || log_warn "T536日志下载失败"

log_info "从RK3576下载ai_server.log..."
scp -P ${RK3576_PORT} ${RK3576_USER}@${RK3576_HOST}:${RK3576_DEST_DIR}/ai_server.log \
    "${LOG_DIR}/ai_server_rk3576.log" 2>/dev/null || log_warn "RK3576日志下载失败"

# 下载AI推理日志
ssh -p ${RK3576_PORT} ${RK3576_USER}@${RK3576_HOST} \
    "ls ${RK3576_DEST_DIR}/ai_inference_*.log 2>/dev/null | head -1" | while read -r rk_log; do
    if [ -n "$rk_log" ]; then
        log_info "下载AI推理日志: $rk_log"
        scp -P ${RK3576_PORT} ${RK3576_USER}@${RK3576_HOST}:${rk_log} \
            "${LOG_DIR}/" 2>/dev/null || true
    fi
done

echo ""
log_info "========== T536 wave_sender.log 内容 =========="
cat "${LOG_DIR}/wave_sender_t536.log" 2>/dev/null || echo "(日志文件为空)"

echo ""
log_info "========== RK3576 ai_server.log 内容 =========="
tail -50 "${LOG_DIR}/ai_server_rk3576.log" 2>/dev/null || echo "(日志文件为空)"

log_info ""
log_info "日志已保存到: ${LOG_DIR}"
log_info "  - wave_sender_t536.log    (T536端采集发送日志)"
log_info "  - ai_server_rk3576.log    (RK3576端AI服务日志)"

# ========== 完成 ==========
echo ""
echo -e "${GREEN}╔══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║     部署和测试完成!                                      ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════════════════════════════╝${NC}"
echo ""
log_info "实时查看T536日志:"
echo "    ssh -p ${T536_PORT} ${T536_USER}@${T536_HOST}"
echo "    cd ${T536_DEST_DIR}"
echo "    tail -f wave_sender.log"
echo ""
log_info "实时查看RK3576日志:"
echo "    ssh -p ${RK3576_PORT} ${RK3576_USER}@${RK3576_HOST}"
echo "    tail -f ${RK3576_DEST_DIR}/ai_server.log"
echo ""
log_info "停止RK3576服务:"
echo "    ssh -p ${RK3576_PORT} ${RK3576_USER}@${RK3576_HOST}"
echo "    pkill -9 -f wave_inference_server"
echo ""
log_info "查看历史测试结果:"
echo "    ls -la ${PROJECT_DIR}/test_results/"
echo ""
