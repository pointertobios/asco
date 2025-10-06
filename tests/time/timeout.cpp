// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/future.h>
#include <asco/print.h>
#include <asco/time/timeout.h>

using asco::future;
using asco::timeout, asco::interval;

using namespace std::chrono_literals;

future<int> async_main() {
    auto res = co_await timeout(1s, [] -> future<void> {
        interval in{2s};
        co_await asco::println("interval start");
        co_await in.tick().aborted([] {});
        if (asco::this_coro::aborted()) {
            co_await asco::println("timeout aborted");
            throw asco::coroutine_abort{};
        } else {
            co_await asco::println("interval 2s");
        }
        co_return;
    });
    if (!res)
        co_await asco::println("timeout");
    else
        co_await asco::println("not timeout");
    co_return 0;
}
