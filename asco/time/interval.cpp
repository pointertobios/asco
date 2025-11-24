// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/time/interval.h>

#include <chrono>

#include <asco/core/runtime.h>
#include <asco/core/worker.h>
#include <asco/yield.h>

namespace asco::time {

future<void> interval::tick() {
    auto now = std::chrono::high_resolution_clock::now();
    auto next_timepoint = last_timepoint + duration;
    if (now < next_timepoint) {
        auto &w = core::worker::this_worker();
        auto tid = w.current_task();
        core::runtime::this_runtime().timer().register_timer(next_timepoint, w, tid);
        w.suspend_task(tid);
        co_await yield<>{};
        last_timepoint = next_timepoint;
    }
    co_return;
}

};  // namespace asco::time
