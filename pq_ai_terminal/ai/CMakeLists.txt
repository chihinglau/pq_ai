# ai/CMakeLists.txt
# AI推理层

set(AI_SRCS
    iforest_infer.c
    ae_infer.c
    cnn1d_infer.c
)

add_library(ai STATIC ${AI_SRCS})
target_link_libraries(ai PUBLIC m)
