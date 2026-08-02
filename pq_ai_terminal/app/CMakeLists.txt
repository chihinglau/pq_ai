# app/CMakeLists.txt
# 嵌入式主入口

set(APP_SRCS
    main.c
)

# 嵌入式目标（当前仅构建占位）
add_executable(pq_terminal ${APP_SRCS})
target_link_libraries(pq_terminal PRIVATE
    core
    ai
    comm
    utils
    drivers
)
