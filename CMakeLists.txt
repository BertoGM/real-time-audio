cmake_minimum_required(VERSION 3.16)
project(audiostream CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)

find_package(Threads REQUIRED)
find_library(SNDFILE_LIB sndfile REQUIRED)
find_library(WEBSOCKETS_LIB websockets REQUIRED)

add_compile_options(-Wall -Wextra -Wpedantic)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O1)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -DNDEBUG)
endif()

# Server
add_executable(audiostream_server src/server.cpp)
target_link_libraries(audiostream_server
    PRIVATE Threads::Threads ${SNDFILE_LIB} ${WEBSOCKETS_LIB})

# Client
add_executable(audiostream_client src/client.cpp)
target_link_libraries(audiostream_client
    PRIVATE Threads::Threads dl m)