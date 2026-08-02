# comm/CMakeLists.txt
# 通信层

set(COMM_SRCS
    proto_mqtt.c
    time_sync.c
)

add_library(comm STATIC ${COMM_SRCS})
