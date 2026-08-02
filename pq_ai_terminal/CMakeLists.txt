cmake_minimum_required(VERSION 3.16)
project(pq_ai_terminal C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(WIN32)
    add_definitions(-DPLATFORM_WINDOWS)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O2")
    link_libraries(winmm ws2_32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_definitions(-DPLATFORM_LINUX)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O2")
else()
    add_definitions(-DPLATFORM_EMBEDDED)
endif()

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/drivers
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/ai
    ${CMAKE_SOURCE_DIR}/comm
    ${CMAKE_SOURCE_DIR}/utils
    ${CMAKE_SOURCE_DIR}/sim
    ${CMAKE_SOURCE_DIR}/app
)

add_subdirectory(drivers)
add_subdirectory(core)
add_subdirectory(ai)
add_subdirectory(comm)
add_subdirectory(utils)
add_subdirectory(sim)
add_subdirectory(app)
