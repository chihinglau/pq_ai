# core/CMakeLists.txt
# 核心算法层

set(CORE_SRCS
    pq_metrics.c
    event_trigger.c
    wave_freeze.c
    feature_extract.c
    scenario_detect.c
)

add_library(core STATIC ${CORE_SRCS})
if(NOT WIN32)
    target_link_libraries(core PUBLIC m)
endif()
