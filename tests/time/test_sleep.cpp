// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <chrono>
#include <print>

#include <asco/future.h>
#include <asco/time/sleep.h>

using namespace asco;
using namespace std::chrono_literals;

future<int> async_main() {
    std::println("Sleeping for 1 second...");
    co_await sleep_for(1s);
    std::println("Awake!");
    std::println("Sleeping until 2 seconds from now...");
    co_await sleep_until(std::chrono::high_resolution_clock::now() + 2s);
    std::println("Awake again!");
    co_return 0;
}
