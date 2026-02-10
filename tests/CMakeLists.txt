# Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
# SPDX-License-Identifier: MIT

add_executable(test_future future.cpp)
target_link_libraries(test_future PRIVATE asco::core asco::main)

add_executable(test_ring_queue concurrency/ring_queue.cpp)
target_link_libraries(test_ring_queue PRIVATE asco::core asco::main)

add_executable(test_hash_map concurrency/hash_map.cpp)
target_link_libraries(test_hash_map PRIVATE asco::core asco::main)
target_compile_definitions(test_hash_map PRIVATE ASCO_TESTING)

add_test(future test_future)
add_test(ring_queue test_ring_queue)
add_test(hash_map test_hash_map)
