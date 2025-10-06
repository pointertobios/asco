// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

// Test for lazy delete of coroutine local frame

#include <asco/coro_local.h>
#include <asco/future.h>
#include <asco/print.h>

using asco::future;

future<void> foo() {
    int *coro_local(arr);
    for (int i = 0; i < 1000; i++) { arr[i] = i; }
    co_await asco::println("foo exited");
    co_return;
}

future<int> async_main() {
    int *decl_local_array(arr, new int[1000]);
    foo();
    co_await asco::println("main exited");
    co_return 0;
}
