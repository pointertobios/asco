# Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
# SPDX-License-Identifier: MIT

add_executable(test_semaphore semaphore.cpp)
target_link_libraries(test_semaphore PRIVATE asco::core asco::test)

add_test(semaphore test_semaphore)

add_executable(test_cancellation cancellation.cpp)
target_link_libraries(test_cancellation PRIVATE asco::core asco::test)

add_test(cancellation test_cancellation)
