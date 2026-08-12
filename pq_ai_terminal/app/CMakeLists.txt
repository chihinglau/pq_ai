# app/CMakeLists.txt
# 嵌入式主入口

# 主程序（仿真用）
add_executable(pq_terminal main.c)
target_link_libraries(pq_terminal PRIVATE
    core
    ai
    comm
    utils
    drivers
)

# 实时波形采集程序
add_executable(pq_acq_wave acq_wave_main.c)
target_link_libraries(pq_acq_wave PRIVATE
    core
    ai
    comm
    utils
    drivers
)

# 安装规则
install(TARGETS pq_terminal pq_acq_wave DESTINATION bin)
