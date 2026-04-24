// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/time/interval.h>

#include <chrono>

#include <asco/time/sleep.h>

namespace asco::time {

future<void> interval::tick() {
    auto now = std::chrono::steady_clock::now();
    if (m_begin_time + m_tick_count * m_duration > now) {
        co_await sleep_until(m_begin_time + m_tick_count * m_duration);
    }
    m_tick_count++;
}

};  // namespace asco::time
