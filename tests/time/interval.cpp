// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <iostream>

#include <asco/future.h>
#include <asco/time/interval.h>

using asco::future, asco::future_void;
using asco::interval;
using namespace std::chrono_literals;

future_void foo() {
    interval in(1s);
    for (int i = 0; i < 10; i++) {
        co_await in.tick();
        std::cout << "tick foo" << std::endl;
    }
    co_return {};
}

future<int> async_main() {
    auto task = foo();
    interval in(500ms);
    for (int i = 0; i < 10; i++) {
        co_await in.tick();
        std::cout << "tick async_main" << std::endl;
    }
    co_await task;
    co_return 0;
}
