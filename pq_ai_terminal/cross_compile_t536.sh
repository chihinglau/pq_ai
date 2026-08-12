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

# 清理旧的构建
rm -rf build-linux

# 使用CMake交叉编译
mkdir -p build-linux
cd build-linux

cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/aarch64-linux-gnu.cmake -DCMAKE_BUILD_TYPE=Release
if [ $? -ne 0 ]; then
    echo "错误: CMake配置失败"
    exit 1
fi

make -j$(nproc)
if [ $? -ne 0 ]; then
    echo "错误: 编译失败"
    exit 1
fi

cd ${PROJECT_DIR}

echo ""
echo "===== Strip 可执行文件 ====="
${STRIP} build-linux/pq_terminal
${STRIP} build-linux/pq_acq_wave

echo ""
echo "===== 验证产物 ====="
echo "主程序 (pq_terminal):"
ls -la build-linux/pq_terminal
file build-linux/pq_terminal
echo ""
echo "波形采集程序 (pq_acq_wave):"
ls -la build-linux/pq_acq_wave
file build-linux/pq_acq_wave

echo ""
echo "===== 检查依赖库 ====="
echo "pq_terminal:"
${CROSS_COMPILE}-readelf -d build-linux/pq_terminal 2>/dev/null | grep NEEDED || echo "  (静态链接)"
echo "pq_acq_wave:"
${CROSS_COMPILE}-readelf -d build-linux/pq_acq_wave 2>/dev/null | grep NEEDED || echo "  (静态链接)"

echo ""
echo "===== 编译完成 ====="
echo "产物目录: ${PROJECT_DIR}/build-linux/"
echo ""
echo "部署到T536:"
echo "  scp build-linux/pq_acq_wave csg@192.168.14.101:/home/csg/"
echo "  ssh -p 8888 csg@192.168.14.101 'chmod +x /home/csg/pq_acq_wave && /home/csg/pq_acq_wave --cycles 100'"
