cmake_minimum_required(VERSION 3.10)
project(asco)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

add_subdirectory(asco)

add_subdirectory(tests)
