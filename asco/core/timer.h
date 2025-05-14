// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_TIMER_H
#define ASCO_TIMER_H 1

#include <algorithm>
#include <chrono>
#include <deque>
#include <semaphore>
#include <thread>
#include <unordered_set>

#include <asco/core/sched.h>
#include <asco/sync/spin.h>
#include <asco/utils/pubusing.h>

namespace asco::core::timer {

using namespace std::chrono;

using task_id = sched::task::task_id;

class timer {
public:
    struct awake_point {
        high_resolution_clock::time_point time;
        std::unordered_set<task_id> id;

        bool operator>(const awake_point &rhs) const { return time > rhs.time; }
    };

    explicit timer();
    ~timer();

    void attach(task_id id, high_resolution_clock::time_point time);
    void detach(task_id id);

    bool task_attaching(task_id id);

private:
    spin<std::deque<awake_point>> awake_points;  // Use least heap
    spin<std::unordered_set<task_id>> attaching_tasks;
    atomic_bool running{true};
    atomic_bool init_waiter{false};
    std::jthread timerthr;

#ifdef __linux__
    ::pthread_t ptid;
#endif

    // Too short for both sleep or spin wait under this duration, merge two awake_point into one.
    constexpr static nanoseconds approx_time = 30ns;

    // Too short for sleep under this duration, just spin wait to ensure the precision.
    constexpr static microseconds nonsleep_time = 1ms;
};

};  // namespace asco::core::timer

#endif
