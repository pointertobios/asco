// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <cstddef>

#include <asco/future.h>
#include <asco/util/types.h>

namespace asco::time {

class interval {
public:
    interval(util::types::duration_type auto duration)
            : m_duration{std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration)} {}

    future<void> tick();

private:
    const std::chrono::steady_clock::duration m_duration;

    const std::chrono::steady_clock::time_point m_begin_time{std::chrono::steady_clock::now()};

    std::size_t m_tick_count{1};
};

};  // namespace asco::time
