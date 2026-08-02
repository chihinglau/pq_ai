# utils/CMakeLists.txt
# 工具层

set(UTILS_SRCS
    ring_buffer.c
    json_builder.c
    sqlite_wrapper.c
    pq_config.c
)

add_library(utils STATIC ${UTILS_SRCS})
