#!/bin/bash
set -e

echo "Building PQ AI Terminal for Linux (aarch64)..."

mkdir -p build-linux
cd build-linux

cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/aarch64-linux-gnu.cmake -DCMAKE_BUILD_TYPE=Release
if [ $? -ne 0 ]; then
    echo "CMake configuration failed!"
    exit 1
fi

make -j$(nproc)
if [ $? -ne 0 ]; then
    echo "Build failed!"
    exit 1
fi

echo "Build completed successfully!"

cd ..
