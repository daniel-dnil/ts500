# Policy
cmake_minimum_required( VERSION 3.8)

project(TS500 VERSION 1.0 LANGUAGES C CXX)
set (CMAKE_CXX_STANDARD 11)

# To build on Windows using mingw in a build subdirectory
# cmake -G "MinGW Makefiles" ..
# cmake --build .
# MSVC is NOT tested

if (WIN32)
    set(WIN32_COMPILE_OPTION -D_WIN32_WINNT=0x0601)
    set(WIN32_LINK_OPTION "-s" "-static-libgcc" "-static-libstdc++")
endif()

add_library(TS500 SHARED
    src/TS500.cpp
    )

target_include_directories(TS500
    PUBLIC
    include
    )

set (COMPILE_OPTIONS
    -Wall -Wpointer-arith -Wshadow
    -Wlogical-op
    -fPIC
    ${WIN32_COMPILE_OPTION}
    )

target_compile_options(TS500 PRIVATE ${COMPILE_OPTIONS} )

if (WIN32)
    target_link_libraries(TS500 "ws2_32")
endif()
