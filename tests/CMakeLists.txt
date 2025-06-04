# Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
# SPDX-License-Identifier: MIT

add_executable(test_futurets futurets.cpp)
target_link_libraries(test_futurets PRIVATE asco asco-main)

add_executable(test_inline inline.cpp)
target_link_libraries(test_inline PRIVATE asco asco-main)

add_executable(test_sems sync/sems.cpp)
target_link_libraries(test_sems PRIVATE asco asco-main)

add_executable(test_clf_lazyde clf_lazyde.cpp)
target_link_libraries(test_clf_lazyde PRIVATE asco asco-main)

add_executable(test_channel channel.cpp)
target_link_libraries(test_channel PRIVATE asco asco-main)

add_executable(test_interval time/interval.cpp)
target_link_libraries(test_interval PRIVATE asco asco-main)

add_executable(test_select select.cpp)
target_link_libraries(test_select PRIVATE asco asco-main)

add_executable(test_timeout time/timeout.cpp)
target_link_libraries(test_timeout PRIVATE asco asco-main)

add_executable(test_sleep time/sleep.cpp)
target_link_libraries(test_sleep PRIVATE asco asco-main)

add_executable(test_rwlock sync/rwlock.cpp)
target_link_libraries(test_rwlock PRIVATE asco asco-main)

add_executable(test_condvar sync/condvar.cpp)
target_link_libraries(test_condvar PRIVATE asco asco-main)

add_executable(test_barrier sync/barrier.cpp)
target_link_libraries(test_barrier PRIVATE asco asco-main)

add_executable(test_unwi unwi.cpp)
target_link_libraries(test_unwi PRIVATE asco asco-main)

add_executable(test_zcbuffer io/zcbuffer.cpp)
target_link_libraries(test_zcbuffer PRIVATE asco asco-main)

add_test(NAME futurets COMMAND test_futurets)
add_test(NAME inline COMMAND test_inline)
add_test(NAME sems COMMAND test_sems)
add_test(NAME clf_lazyde COMMAND test_clf_lazyde)
add_test(NAME channel COMMAND test_channel)
add_test(NAME interval COMMAND test_interval)
add_test(NAME select COMMAND test_select)
add_test(NAME timeout COMMAND test_timeout)
add_test(NAME sleep COMMAND test_sleep)
add_test(NAME rwlock COMMAND test_rwlock)
add_test(NAME condvar COMMAND test_condvar)
add_test(NAME barrier COMMAND test_barrier)
add_test(NAME unwi COMMAND test_unwi)
add_test(NAME zcbuffer COMMAND test_zcbuffer)
