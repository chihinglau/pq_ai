# drivers/CMakeLists.txt
# HT7627S驱动层 (当前仅有寄存器定义头文件)

# 纯头文件库，无需编译源文件
add_library(drivers INTERFACE)
target_include_directories(drivers INTERFACE ${CMAKE_SOURCE_DIR}/include)
