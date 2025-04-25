// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_TIMER_H
#define ASCO_TIMER_H 1

#include <algorithm>
#include <chrono>
#include <deque>
#include <semaphore>
#include <thread>
#include <vector>

#include <asco/core/sched.h>
#include <asco/sync/spin.h>
#include <asco/utils/pubusing.h>

namespace asco::timer {

using namespace std::chrono;

class timer {
public:
    struct awake_point {
        high_resolution_clock::time_point time;
        std::vector<sched::task::task_id> id;

        bool operator>(const awake_point &rhs) const { return time > rhs.time; }
    };

    explicit timer();
    ~timer();

    void attach(sched::task::task_id id, high_resolution_clock::time_point time);

private:
    spin<std::deque<awake_point>> awake_points;  // Use least heap
    atomic_bool running{true};
    atomic_bool init_waiter{false};
    std::jthread timerthr;

    ::pthread_t ptid;

    // Too short for both sleep or spin wait under this duration, merge two awake_point into one.
    constexpr static nanoseconds approx_time = 30ns;

    // Too short for sleep under this duration, just spin wait to ensure the precision.
    constexpr static microseconds nonsleep_time = 1ms;
};

};  // namespace asco::timer

#endif
