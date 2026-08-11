#!/bin/bash
# PQ AI Terminal 交叉编译脚本 (T536 aarch64)
# 交叉编译器: gcc-linaro-11.3.1 aarch64-linux-gnu
# 目标平台: T536 (Linux 5.10.198 aarch64)

set -e

SCRIPT_DIR=$(cd $(dirname $0) && pwd)
PROJECT_DIR=$(dirname $SCRIPT_DIR)

CROSS_COMPILE=/opt/toolchain/gcc-linaro-11.3.1-2022.06-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu

export PATH=$(dirname ${CROSS_COMPILE}):$PATH

CC=${CROSS_COMPILE}-gcc
STRIP=${CROSS_COMPILE}-strip

# 检查交叉编译器
if [ ! -e ${CC} ]; then
    echo "错误: 交叉编译器不存在 ${CC}"
    exit 1
fi

echo "===== PQ AI Terminal 交叉编译 (T536 aarch64) ====="
echo "交叉编译器: ${CC}"
echo "项目目录: ${PROJECT_DIR}"
echo ""

cd ${PROJECT_DIR}

# 清理
make clean 2>/dev/null || true

# 编译
make CROSS_CC="${CC}" CROSS_CFLAGS="-std=c99 -Wall -Wextra -O2 -DPLATFORM_LINUX" linux-arm

if [ $? -ne 0 ]; then
    echo "错误: 编译失败"
    exit 1
fi

echo ""
echo "===== Strip 可执行文件 ====="
${STRIP} pq_terminal_arm

echo ""
echo "===== 验证产物 ====="
ls -la pq_terminal_arm
file pq_terminal_arm

echo ""
echo "===== 检查依赖库 ====="
${CROSS_COMPILE}-readelf -d pq_terminal_arm 2>/dev/null | grep NEEDED || \
    ${CROSS_COMPILE}-objdump -x pq_terminal_arm 2>/dev/null | grep NEEDED || \
    ldd pq_terminal_arm 2>/dev/null || echo "  (静态链接或无需额外依赖)"

echo ""
echo "===== 编译完成 ====="
echo "可执行文件: ${PROJECT_DIR}/pq_terminal_arm"
echo "大小: $(ls -lh pq_terminal_arm | awk '{print $5}')"
echo "架构: $(file pq_terminal_arm | grep -o 'ARM.*')"
