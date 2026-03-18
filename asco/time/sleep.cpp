// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/time/sleep.h>

namespace asco::time {

future<void> sleep_until(std::chrono::steady_clock::time_point time_point) {
    auto &rt = core::runtime::current();
    auto &w = core::worker::current();
    auto &timer = rt.get_timer();

    auto hdl = w.this_coroutine();
    auto tmid = timer.register_timer(time_point, hdl);

    if (!tmid) {
        co_return;
    }

    cancel_callback cb{this_task::get_cancel_token(), [&timer, tmid = *tmid]() { timer.cancel_timer(tmid); }};
    w.suspend_current_handle(hdl);
    co_await this_task::yield();
}

};  // namespace asco::time
