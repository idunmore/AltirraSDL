# Standalone CMake build for the AltirraBridge C SDK examples.
#
# This file does NOT depend on the parent AltirraSDL CMake build.
# It builds against just the SDK header + .c file in the parent
# directory, so users who unpack a release package and want to write
# C clients can build the examples in isolation:
#
#   cd AltirraBridge/sdk/c/examples
#   mkdir build && cd build
#   cmake ..
#   cmake --build .

cmake_minimum_required(VERSION 3.16)
project(altirra_bridge_examples C)

set(CMAKE_C_STANDARD 99)

add_executable(01_ping
    01_ping.c
    ../altirra_bridge.c
)
target_include_directories(01_ping PRIVATE ..)

add_executable(02_peek_regs
    02_peek_regs.c
    ../altirra_bridge.c
)
target_include_directories(02_peek_regs PRIVATE ..)

if(WIN32)
    target_link_libraries(01_ping       PRIVATE ws2_32)
    target_link_libraries(02_peek_regs  PRIVATE ws2_32)
endif()
