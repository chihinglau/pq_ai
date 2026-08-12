# drivers/CMakeLists.txt
# HT7627S驱动层 + 波形采样HAL

add_library(drivers STATIC
    wave_sampler_hal.c
)
target_include_directories(drivers PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/drivers
)
