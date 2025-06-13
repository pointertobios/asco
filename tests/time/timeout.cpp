// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <iostream>

#include <asco/future.h>
#include <asco/time/timeout.h>

using asco::future, asco::future_void;
using asco::timeout, asco::interval;

using namespace std::chrono_literals;

future<int> async_main() {
    auto res = co_await timeout(1s, [] -> future_void {
        interval in{2s};
        std::cout << "interval start\n";
        co_await in.tick().aborted([] {});
        if (asco::this_coro::aborted()) {
            std::cout << "timeout aborted\n";
            throw asco::coroutine_abort{};
        } else {
            std::cout << "interval 2s\n";
        }
        co_return {};
    });
    if (!res)
        std::cout << "timeout\n";
    else
        std::cout << "not timeout\n";
    co_return 0;
}
