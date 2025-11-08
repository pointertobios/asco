# Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
# SPDX-License-Identifier: MIT

add_executable(test_future_base future_base.cpp)
target_link_libraries(test_future_base PRIVATE asco::core asco::main)

add_executable(test_semaphore sync/test_semaphore.cpp)
target_link_libraries(test_semaphore PRIVATE asco::core asco::main)

add_test(NAME future_base COMMAND test_future_base)
add_test(NAME semaphore COMMAND test_semaphore)
