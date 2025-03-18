cmake_minimum_required(VERSION 3.10)
project(asco)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ASCO_SOURCES
    asco/runtime.cpp
)

add_library(asco ${ASCO_SOURCES})
target_include_directories(asco PUBLIC .)

add_executable(test_corotest tests/corotest.cpp)
target_link_libraries(test_corotest PRIVATE asco)

add_executable(inner_channel_test tests/inner_channel_test.cpp)
target_link_libraries(inner_channel_test PRIVATE asco)
