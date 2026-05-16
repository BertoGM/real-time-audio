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

option(USE_OPUS "Enable Opus codec" OFF)

add_compile_options(-Wall -Wextra -Wpedantic)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-fsanitize=address -fsanitize=undefined -g -O1)
    add_link_options   (-fsanitize=address -fsanitize=undefined)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -DNDEBUG)
endif()

set(AUDIO_LIBS dl m)

add_executable(audiostream_server src/server.cpp)
target_link_libraries(audiostream_server
    PRIVATE Threads::Threads ${SNDFILE_LIB})

add_executable(audiostream_client src/client.cpp)
target_link_libraries(audiostream_client
    PRIVATE Threads::Threads ${AUDIO_LIBS})