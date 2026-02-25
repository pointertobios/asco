# Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
# SPDX-License-Identifier: MIT

add_executable(tests
    cancellation.cpp
    sync/semaphore.cpp
    task_local.cpp
)
target_link_libraries(tests PRIVATE asco::core asco::test)

add_test(tests tests)
