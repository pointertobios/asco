// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/time/sleep.h>

#include <chrono>

#include <asco/cancellation.h>
#include <asco/core/runtime.h>
#include <asco/core/worker.h>
#include <asco/yield.h>

namespace asco::time {

future<void> sleep_until(std::chrono::steady_clock::time_point time_point) {
    auto &timer = core::runtime::current().get_timer();

    core::awake_token token{};
    auto tmid = timer.register_timer(time_point, token);

    if (!tmid) {
        co_return;
    }

    cancel_callback cb{[&timer, tmid = *tmid]() { timer.cancel_timer(tmid); }};
    token.suspend();
    co_await this_task::yield();
}

};  // namespace asco::time
