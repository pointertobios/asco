// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/future.h>
#include <asco/print.h>
#include <asco/time/sleep.h>

using asco::future;

using namespace std::chrono_literals;

future<int> async_main() {
    co_await asco::this_coro::sleep_for(1s);
    asco::println("1s");
    co_await asco::this_coro::sleep_until(std::chrono::system_clock::now() + 1s);
    asco::println("2s");
    co_return 0;
}
