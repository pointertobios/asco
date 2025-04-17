# Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(inner_channel_test inner_channel_test.cpp)
target_link_libraries(inner_channel_test PRIVATE asco)

add_executable(test_futurets futurets.cpp)
target_link_libraries(test_futurets PRIVATE asco)

add_executable(test_inline inline.cpp)
target_link_libraries(test_inline PRIVATE asco)

add_executable(test_sems sems.cpp)
target_link_libraries(test_sems PRIVATE asco)

add_test(NAME inner_channel COMMAND inner_channel_test)
add_test(NAME futurets COMMAND test_futurets)
add_test(NAME inline COMMAND test_inline)
add_test(NAME sems COMMAND test_sems)
