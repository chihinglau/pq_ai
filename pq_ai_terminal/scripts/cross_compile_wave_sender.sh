#!/bin/bash
#
# wave_sender_arm 交叉编译脚本
# 编译T536端波形发送程序 (ARM 32位)
#

set -e

# ========== 配置 ==========
CROSS_COMPILER=/opt/scm/gcc-linaro-5.3.1-2016.05-x86_64_arm-linux-gnueabihf/bin
CC=${CROSS_COMPILER}/arm-linux-gnueabihf-gcc
SYSROOT=/opt/scm/gcc-linaro-5.3.1-2016.05-x86_64_arm-linux-gnueabihf/arm-linux-gnueabihf/libc

# 项目路径
PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
SRC_DIR=${PROJECT_DIR}/app
DRIVERS_DIR=${PROJECT_DIR}/drivers
CORE_DIR=${PROJECT_DIR}/core
HAL_LIB_DIR=${PROJECT_DIR}/lib

# 输出目录
BUILD_DIR=${PROJECT_DIR}/build
OUTPUT_DIR=${PROJECT_DIR}/deploy

# 输出文件名
TARGET=wave_sender_arm

# ========== 检查环境 ==========
echo "============================================"
echo "  wave_sender_arm 交叉编译"
echo "============================================"
echo ""
echo "交叉编译器: ${CC}"
echo "sysroot: ${SYSROOT}"
echo "项目目录: ${PROJECT_DIR}"
echo ""

if [ ! -f "${CC}" ]; then
    echo "[ERROR] 交叉编译器不存在: ${CC}"
    echo "请检查工具链路径"
    exit 1
fi

# ========== 创建目录 ==========
mkdir -p ${BUILD_DIR}
mkdir -p ${OUTPUT_DIR}
mkdir -p ${OUTPUT_DIR}/lib

# ========== 编译 ==========
echo "[编译] 编译 wave_sender_arm.c ..."

${CC} \
    -o ${BUILD_DIR}/${TARGET} \
    ${SRC_DIR}/wave_sender_arm.c \
    ${DRIVERS_DIR}/wave_sampler_hal.c \
    -I${DRIVERS_DIR} \
    -I${SYSROOT}/usr/include \
    -I${SYSROOT}/usr/include/linux \
    -L${SYSROOT}/usr/lib \
    -L${SYSROOT}/lib \
    -L${HAL_LIB_DIR} \
    -lhd -ldrivers -lpthread -lm -lrt -ldl \
    -static-libgcc -static-libstdc++ \
    -Wl,-rpath,'$ORIGIN/lib' \
    -Wl,-rpath,./lib \
    -Wl,-rpath,/custom/sys/lib/hal_lib/lib32 \
    -Wno-unused-function \
    -O2

echo "[编译] 编译完成"

# ========== 验证 ==========
echo ""
echo "[验证] 检查可执行文件..."

file ${BUILD_DIR}/${TARGET}
ls -lh ${BUILD_DIR}/${TARGET}

# ========== 检查依赖 ==========
echo ""
echo "[依赖] 检查动态依赖..."

if command -v arm-linux-gnueabihf-readelf &> /dev/null; then
    arm-linux-gnueabihf-readelf -d ${BUILD_DIR}/${TARGET} | grep "NEEDED" || true
else
    readelf -d ${BUILD_DIR}/${TARGET} 2>/dev/null | grep "NEEDED" || echo "  (无法检查依赖，需要arm-linux-gnueabihf-readelf)"
fi

# ========== 部署 ==========
echo ""
echo "[部署] 准备部署包..."

# 复制可执行文件
cp ${BUILD_DIR}/${TARGET} ${OUTPUT_DIR}/

# 复制依赖库
echo "  复制依赖库..."

# 从HAL库目录复制
if [ -d "${HAL_LIB_DIR}" ]; then
    cp ${HAL_LIB_DIR}/libhd.so* ${OUTPUT_DIR}/lib/ 2>/dev/null || true
    cp ${HAL_LIB_DIR}/libdrivers.so* ${OUTPUT_DIR}/lib/ 2>/dev/null || true
    cp ${HAL_LIB_DIR}/libhal_device.so* ${OUTPUT_DIR}/lib/ 2>/dev/null || true
fi

# 从sysroot复制32位运行时库
cp ${SYSROOT}/usr/lib/libhd.so* ${OUTPUT_DIR}/lib/ 2>/dev/null || true
cp ${SYSROOT}/usr/lib/libdrivers.so* ${OUTPUT_DIR}/lib/ 2>/dev/null || true

# 创建必要的软链接
cd ${OUTPUT_DIR}/lib
for lib in *.so.*; do
    if [ -f "$lib" ]; then
        base=$(echo "$lib" | sed 's/\.so\..*/\.so/')
        if [ ! -f "$base" ]; then
            ln -sf "$lib" "$base"
        fi
    fi
done
cd ${OUTPUT_DIR}

# 创建启动脚本
cat > ${OUTPUT_DIR}/run.sh << 'EOF'
#!/bin/bash
# wave_sender_arm 启动脚本

# 设置路径
export LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH

# 确保lib目录存在
mkdir -p lib

# 设置32位动态链接器
if [ -f /lib32/ld-linux-armhf.so.3 ]; then
    LINKER=/lib32/ld-linux-armhf.so.3
elif [ -f /custom/sys/lib/hal_lib/lib32/ld-linux-armhf.so.3 ]; then
    LINKER=/custom/sys/lib/hal_lib/lib32/ld-linux-armhf.so.3
else
    LINKER=/lib/ld-linux-armhf.so.3
fi

# 运行程序
echo "============================================"
echo "  T536 波形采集发送程序"
echo "  功能: 采集波形 → 发送RK3576 → 接收推理"
echo "============================================"
echo ""

if [ -f "$LINKER" ]; then
    $LINKER --library-path ./lib:/lib32 ./wave_sender_arm "$@"
else
    export LD_LIBRARY_PATH=./lib:/lib32
    ./wave_sender_arm "$@"
fi
EOF

chmod +x ${OUTPUT_DIR}/run.sh

# ========== 打包 ==========
echo ""
echo "[打包] 创建部署包..."

DEPLOY_TARGET=wave_sender_arm_armhf

cd ${OUTPUT_DIR}
tar czf ${DEPLOY_TARGET}.tar.gz \
    wave_sender_arm \
    lib/ \
    run.sh

echo ""
echo "============================================"
echo "  编译完成!"
echo "============================================"
echo ""
echo "可执行文件: ${BUILD_DIR}/${TARGET}"
echo "部署目录:   ${OUTPUT_DIR}"
echo "部署包:     ${OUTPUT_DIR}/${DEPLOY_TARGET}.tar.gz"
echo ""
echo "[使用方法]"
echo "  1. 上传部署包到T536:"
echo "     scp -P 8888 ${DEPLOY_TARGET}.tar.gz csg@192.168.14.101:~/"
echo ""
echo "  2. SSH登录T536并解压:"
echo "     ssh -p 8888 csg@192.168.14.101"
echo "     cd ~ && tar xzf ${DEPLOY_TARGET}.tar.gz"
echo ""
echo "  3. 运行程序:"
echo "     cd ~/${DEPLOY_TARGET}"
echo "     ./run.sh --cycles 5 --server 192.168.100.1 --port 9090"
echo ""
echo "  4. 查看日志:"
echo "     tail -f wave_sender.log"
echo ""
echo "[RK3576端启动AI服务]"
echo "  ssh cat@192.168.137.204"
echo "  python3 wave_inference_server.py --host 192.168.100.1 --port 9090"
echo ""