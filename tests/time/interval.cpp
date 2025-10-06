// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/future.h>
#include <asco/print.h>
#include <asco/time/interval.h>

using asco::future;
using asco::interval;
using namespace std::chrono_literals;

future<void> foo() {
    interval in(1s);
    for (int i = 0; i < 10; i++) {
        co_await in.tick();
        co_await asco::println("tick foo");
    }
    co_return;
}

future<int> async_main() {
    auto task = foo();
    interval in(500ms);
    for (int i = 0; i < 10; i++) {
        co_await in.tick();
        co_await asco::println("tick async_main");
    }
    co_await task;
    co_return 0;
}
