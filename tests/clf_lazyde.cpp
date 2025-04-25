// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Test for lazy delete of coroutine local frame

#include <iostream>

#include <asco/coro_local.h>
#include <asco/future.h>

using asco::future, asco::future_void;

future_void foo() {
    int *coro_local(arr);
    for (int i = 0; i < 1000; i++) {
        arr[i] = i;
    }
    std::cout << "foo exited" << std::endl;
    co_return {};
}

future<int> async_main() {
    int *decl_local_array(arr, new int[1000]);
    foo();
    std::cout << "main exited" << std::endl;
    co_return 0;
}
