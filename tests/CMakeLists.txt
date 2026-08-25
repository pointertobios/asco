# Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
# SPDX-License-Identifier: MIT

add_executable(asco_unit_tests
    sync/condition_variable.cpp
)

target_link_libraries(asco_unit_tests PRIVATE asco::test)
add_test(unit_tests asco_unit_tests)
