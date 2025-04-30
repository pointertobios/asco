// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef __linux__
#    include <cpuid.h>
#    include <pthread.h>
#    include <signal.h>
#endif

#include <asco/core/runtime.h>
#include <asco/core/timer.h>

namespace asco::timer {

timer::timer()
        : timerthr([this] {
#ifdef __linux__
            ::pthread_setname_np(::pthread_self(), std::format("asco::timer").c_str());
            ptid = ::pthread_self();

            signal(SIGURG, [](int) {});
#endif
            init_waiter.store(true, morder::seq_cst);

            while (running.load(morder::seq_cst)) {
                if (awake_points.lock()->empty()) {
                    std::this_thread::sleep_for(5ms);
                }
                if (awake_points.lock()->empty())
                    continue;

                if (auto guard = awake_points.lock(); guard->front().time <= high_resolution_clock::now()) {
                    auto &point = guard->front();
                    for (auto &id : point.id) {
                        runtime::get_runtime()->awake(id);
                    }
                    guard->pop_front();
                    continue;
                }

                for (auto t{awake_points.lock()->front().time}; t > high_resolution_clock::now();
                     t = awake_points.lock()->front().time) {
                    // Sleep if waiting time longer than `nonsleep_time`.
                    // Don't worry about new awake points attached during sleeping, in attach function will
                    // send SIG_BLOCK to break sleeping.
                    //
                    // There are several levels of sleep time to reduce running time and reduce deviations.
                    if (t - high_resolution_clock::now() > nonsleep_time * 10) {
                        std::this_thread::sleep_for(5ms);
                    } else if (t - high_resolution_clock::now() > nonsleep_time * 5) {
                        std::this_thread::sleep_for(2ms);
                    } else if (t - high_resolution_clock::now() > nonsleep_time) {
                        std::this_thread::sleep_for(1ms);
                    }
                }
            }
        }) {
}

timer::~timer() {
    running.store(false, morder::seq_cst);
    timerthr.join();
}

void timer::attach(sched::task::task_id id, high_resolution_clock::time_point time) {
    auto guard = awake_points.lock();
    if (auto it = std::find_if(
            guard->begin(), guard->end(),
            [time](awake_point &point) {
                return (point.time > time ? point.time - time : time - point.time) <= approx_time;
            });
        it != guard->end()) {
        it->id.push_back(id);
    }

    guard->push_back({time, {id}});
    std::push_heap(guard->begin(), guard->end(), std::greater<awake_point>());
    while (!init_waiter.load(morder::seq_cst));
#ifdef __linux__
    ::pthread_kill(ptid, SIGURG);
#endif
}

};  // namespace asco::timer
