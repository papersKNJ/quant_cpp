cmake_minimum_required(VERSION 3.10)

project(QuantCpp)

set(CMAKE_CXX_STANDARD 17)  # C++17 표준 사용
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_SOURCE_DIR})  # 기본 경로 추가

add_executable(main main.cpp)