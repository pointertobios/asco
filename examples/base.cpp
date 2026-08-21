// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <print>

#include "asco/future.h"
#include "asco/this_task.h"

using namespace asco;

future<int> foo(int x) { co_return x; }

future<int> async_main() {
    auto x = co_await foo(42);
    auto handle = this_task::spawn([] -> future<> {
        for (auto i = 0; i < 10; i++) {
            std::println("long task {}", i);
            co_await this_task::yield();
        }
    });
    std::println("{}", x);
    co_await handle;
    co_return 0;
}
