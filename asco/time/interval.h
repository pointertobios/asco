// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>

#include <asco/future.h>
#include <asco/utils/concepts.h>

namespace asco::time {

using namespace concepts;

class interval {
public:
    template<duration_type Dur>
    explicit interval(const Dur &duration)
            : duration{std::chrono::duration_cast<std::chrono::nanoseconds>(duration)}
            , last_timepoint{std::chrono::high_resolution_clock::now()} {}

    future<void> tick();

private:
    const std::chrono::nanoseconds duration;
    std::chrono::high_resolution_clock::time_point last_timepoint;
};

};  // namespace asco::time

namespace asco {

using interval = time::interval;

};
