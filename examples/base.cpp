// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <chrono>
#include <print>
#include <ranges>

#include "asco/future.h"
#include "asco/sync/mpsc.h"
#include "asco/this_task.h"

using namespace asco;

future<int> foo(int x) { co_return x; }

future<int> async_main() {
    using namespace std::chrono;

    auto [tx, rx] = sync::mpsc<steady_clock::time_point>::channel();
    usize sum = 0;

    auto h1 = this_task::spawn([&] -> future<> {
        for (auto i = 0; i < 1000; i++) {
            (void)i;
            co_await tx.send(steady_clock::now());
            co_await this_task::yield();
        }
        co_return;
    });
    auto h2 = this_task::spawn([&] -> future<> {
        for (auto i = 0; i < 1000; i++) {
            (void)i;
            auto t = co_await rx.recv();
            auto dur = steady_clock::now() - t;
            sum += dur.count();
        }
        co_return;
    });

    co_await h1;
    co_await h2;

    std::println("{}us", static_cast<double>(sum) / 1000);

    co_return 0;
}
