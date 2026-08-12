# 设置 CMake 最低可用版本
cmake_minimum_required(VERSION 3.10)

# 禁用基于编译器的依赖生成（gcc-linaro-5.3.1 有 -MD/-MF bug）
set(CMAKE_DEPENDS_USE_COMPILER FALSE)

# 指定交叉编译（必须在 project() 之前设置）
SET(CMAKE_SYSTEM_NAME Linux)
SET(CMAKE_C_COMPILER /opt/scm/gcc-linaro-5.3.1-2016.05-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-gcc)
SET(CMAKE_CXX_COMPILER /opt/scm/gcc-linaro-5.3.1-2016.05-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-c++)

# 设置工程名称
project(WaveExportARM)

# 设置变量 C 标准为 99
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 11)

# 增加编译参数
add_definitions(" -Wno-narrowing -g -O2 -rdynamic -funwind-tables -ffunction-sections")

# 全局包含目录
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/drivers)
include_directories(${PROJECT_SOURCE_DIR}/core)
include_directories(${PROJECT_SOURCE_DIR}/app)

# 设置二进制文件路径
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

# 添加可执行文件
add_executable(wave_export_arm 
    app/wave_export_arm.c
)

# 对所有可执行文件生效
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ffunction-sections -fdata-sections")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections,-Map=output.map")

# 添加库链接 - 只链接HAL核心库，其他库使用dlopen动态加载
target_link_libraries(wave_export_arm
    m
    nsl
    pthread
    dl
    ${PROJECT_SOURCE_DIR}/lib/libhd.so
)
