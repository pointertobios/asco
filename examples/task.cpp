// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <print>

#include "asco/future.h"
#include "asco/this_task.h"

using namespace asco;

future<int> async_main() {
    core::worker_handle h{};
    auto t1 = this_task::spawn(h, [] -> future<> {
        std::println("t1 on {}", this_task::worker().id());
        co_return;
    });
    auto t2 = this_task::spawn(h, [] -> future<> {
        std::println("t2 on {}", this_task::worker().id());
        co_return;
    });
    const auto &main_wh = this_task::worker_handle();
    std::println("async main on {}", this_task::worker().id());
    auto t3 = this_task::spawn(main_wh, [] -> future<> {
        std::println("t3 on {}", this_task::worker().id());
        co_return;
    });
    co_await t1;
    co_await t2;
    co_await t3;
    co_return 0;
}
