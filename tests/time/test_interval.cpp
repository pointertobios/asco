// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <print>

#include <asco/future.h>
#include <asco/time/interval.h>

using namespace asco;
using namespace std::chrono_literals;

future<int> async_main() {
    interval iv{500ms};
    for (int i = 0; i < 5; ++i) {
        co_await iv.tick();
        std::println("Tick {}", i + 1);
    }
    co_return 0;
}
