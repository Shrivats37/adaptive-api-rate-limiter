cmake_minimum_required(VERSION 3.12)
project(ApiRateLimiter VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard to C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include header directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Executable target
add_executable(rate_limiter_app src/main.cpp)
