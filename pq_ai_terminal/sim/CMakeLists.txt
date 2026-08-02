# sim/CMakeLists.txt
# Windows仿真环境

set(SIM_SRCS
    hal_sim.c
    sim_main.c
)

add_executable(pq_sim ${SIM_SRCS})
target_link_libraries(pq_sim PRIVATE
    core
    ai
    comm
    utils
    drivers
)

if(WIN32)
    target_link_libraries(pq_sim PRIVATE winmm ws2_32)
endif()
