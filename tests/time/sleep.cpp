// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <iostream>

#include <asco/future.h>
#include <asco/time/sleep.h>

using asco::future;

using namespace std::chrono_literals;

future<int> async_main() {
    co_await asco::this_coro::sleep_for(1s);
    std::cout << "1s\n";
    co_await asco::this_coro::sleep_until(std::chrono::system_clock::now() + 1s);
    std::cout << "2s\n";
    co_return 0;
}
