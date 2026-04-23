
cmake_minimum_required(VERSION 3.10)
project(TLSFAllocator)

set(CMAKE_CXX_STANDARD 17)

add_executable(code main.cpp allocator.cpp)
target_include_directories(code PRIVATE .)
