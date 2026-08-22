cmake_minimum_required(VERSION 3.10)
project(torus)

# default to an optimized build so a forgotten -DCMAKE_BUILD_TYPE doesn't
# silently produce a 16x slower binary
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(torus INTERFACE)
target_include_directories(torus INTERFACE include)

# Benchmarks
add_subdirectory(benchmarks)

# Tests
add_subdirectory(tests)

