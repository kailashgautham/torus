cmake_minimum_required(VERSION 3.10)
project(torus)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(torus INTERFACE)
target_include_directories(torus INTERFACE include)

# Benchmarks
add_subdirectory(benchmarks)

# Tests
add_subdirectory(tests)

