# Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
# SPDX-License-Identifier: MIT

add_executable(test_future_base future_base.cpp)
target_link_libraries(test_future_base PRIVATE asco::core asco::main)

add_executable(test_semaphore sync/test_semaphore.cpp)
target_link_libraries(test_semaphore PRIVATE asco::core asco::main)

add_executable(test_interval time/test_interval.cpp)
target_link_libraries(test_interval PRIVATE asco::core asco::main)

add_executable(test_sleep time/test_sleep.cpp)
target_link_libraries(test_sleep PRIVATE asco::core asco::main)

add_executable(test_channel sync/test_channel.cpp)
target_link_libraries(test_channel PRIVATE asco::core asco::main)

add_executable(test_generator test_generator.cpp)
target_link_libraries(test_generator PRIVATE asco::core asco::main)

add_executable(test_notify sync/test_notify.cpp)
target_link_libraries(test_notify PRIVATE asco::core asco::main)

add_executable(test_context context/test_context.cpp)
target_link_libraries(test_context PRIVATE asco::core asco::main)

add_test(NAME future_base COMMAND test_future_base)
add_test(NAME semaphore COMMAND test_semaphore)
add_test(NAME interval COMMAND test_interval)
add_test(NAME sleep COMMAND test_sleep)
add_test(NAME channel COMMAND test_channel)
add_test(NAME generator COMMAND test_generator)
add_test(NAME notify COMMAND test_notify)
add_test(NAME context COMMAND test_context)
