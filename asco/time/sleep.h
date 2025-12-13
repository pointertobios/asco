// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>

#include <asco/future.h>
#include <asco/utils/concepts.h>
#include <asco/yield.h>

namespace asco::time {

using namespace concepts;

future<void> sleep_for(const duration_type auto &duration) {
    auto &w = core::worker::this_worker();
    auto tid = w.current_task();
    auto expire_time = std::chrono::high_resolution_clock::now()
                       + std::chrono::duration_cast<std::chrono::nanoseconds>(duration);
    core::runtime::this_runtime().timer().register_timer(expire_time, w, tid);
    w.suspend_task(tid);
    co_await yield<>{};
    co_return;
}

future<void> sleep_until(const time_point_type auto &timepoint) {
    auto &w = core::worker::this_worker();
    auto tid = w.current_task();
    core::runtime::this_runtime().timer().register_timer(timepoint, w, tid);
    w.suspend_task(tid);
    co_await yield<>{};
    co_return;
}

};  // namespace asco::time

namespace asco {

using time::sleep_for;
using time::sleep_until;

};  // namespace asco
