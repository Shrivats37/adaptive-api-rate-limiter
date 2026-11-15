cmake_minimum_required(VERSION 3.12)
project(AdaptiveRateLimiter VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)   # -O3: benchmarks must be optimised
endif()

find_package(Threads REQUIRED)
include_directories(${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/third_party)

add_executable(rate_limiter_demo   src/demo.cpp)
add_executable(rate_limiter_bench  src/bench.cpp)
add_executable(rate_limiter_server src/server.cpp)
add_executable(test_all            tests/test_all.cpp)
foreach(t rate_limiter_demo rate_limiter_bench rate_limiter_server test_all)
  target_link_libraries(${t} PRIVATE Threads::Threads)
  target_compile_options(${t} PRIVATE -Wall -Wextra)
endforeach()

enable_testing()
add_test(NAME all COMMAND test_all)
