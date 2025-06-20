// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/timer.h>

#ifdef __linux__
#    include <pthread.h>
#    include <signal.h>
#endif

#include <asco/core/runtime.h>

namespace asco::core::timer {

timer::timer()
        : daemon("asco::timer", SIGALRM) {
    daemon::start();
}

void timer::attach(task_id id, high_resolution_clock::time_point time) {
    auto g = attaching_tasks.lock();
    g->insert(id);
    auto guard = awake_points.lock();
    if (auto it = std::find_if(
            guard->begin(), guard->end(),
            [time](awake_point &point) {
                return (point.time > time ? point.time - time : time - point.time) <= approx_time;
            });
        it != guard->end()) {
        it->id.insert(id);
    }

    guard->push_back({time, {id}});
    std::push_heap(guard->begin(), guard->end(), std::greater<awake_point>());
    daemon::awake();
}

void timer::detach(task_id id) {
    auto g = attaching_tasks.lock();
    g->erase(id);
    auto guard = awake_points.lock();
    if (auto it = std::find_if(
            guard->begin(), guard->end(), [id](const auto &point) { return point.id.contains(id); });
        it != guard->end()) {
        it->id.erase(id);
        if (it->id.empty())
            guard->erase(it);
    }
    std::make_heap(guard->begin(), guard->end(), std::greater<awake_point>{});
}

bool timer::task_attaching(task_id id) { return attaching_tasks.lock()->contains(id); }

void timer::run() {
    if (awake_points.lock()->empty()) {
        std::this_thread::sleep_for(5ms);
    }
    if (awake_points.lock()->empty())
        return;

    if (auto guard = awake_points.lock(); guard->front().time <= high_resolution_clock::now()) {
        auto &point = guard->front();
        for (auto &id : point.id) {
            attaching_tasks.lock()->erase(id);
            if (RT::__worker::task_available(id))
                RT::get_runtime().awake(id);
        }
        std::pop_heap(guard->begin(), guard->end(), std::greater<awake_point>());
        guard->pop_back();

        return;
    }

    if (awake_points.lock()->empty())
        return;

    for (auto t{awake_points.lock()->front().time}; t > high_resolution_clock::now(); ({
             if (auto guard = awake_points.lock(); !guard->empty())
                 t = guard->front().time;
         })) {
        if (awake_points.lock()->empty())
            break;

        auto st = t - high_resolution_clock::now();

        // buzy wait when st is short enough
        if (st < 1ms)
            continue;

        std::this_thread::interruptable_sleep_for(st);

        if (awake_points.lock()->empty())
            break;
    }
}

};  // namespace asco::core::timer
