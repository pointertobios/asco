// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/time/high_resolution_timer.h>

#include <chrono>
#include <ranges>

namespace asco::core::time {

high_resolution_timer::high_resolution_timer()
        : daemon{"asco::timer"} {
    auto _ = daemon::start();
}

bool high_resolution_timer::init() { return true; }

bool high_resolution_timer::run_once(std::stop_token &) {
    if (timer_tree.lock()->empty()) {
        sleep_until_awake();
        return true;
    }

    timer_entry e = ({
        alignas(alignof(timer_entry)) char res[sizeof(timer_entry)];
        if (auto g = timer_tree.lock()) {
            if (auto &es = g->begin()->second.entries; !es.empty()) {
                new (&res) timer_entry{es.begin()->second};
            } else {
                g->erase(g->begin());
                return true;
            }
        }
        *reinterpret_cast<timer_entry *>(&res);
    });
    sleep_until_awake_before(e.expire_time);
    if (std::chrono::high_resolution_clock::now() >= e.expire_time) {
        e.worker_ref.activate_task(e.tid);
        if (auto g = timer_tree.lock(); g->contains(e.expire_seconds_since_epoch())) {
            if (auto &es = g->at(e.expire_seconds_since_epoch()).entries; es.contains(e.id()))
                es.erase(e.id());
        }
    }

    return true;
}

void high_resolution_timer::shutdown() {}

timer_id high_resolution_timer::register_timer(
    const std::chrono::high_resolution_clock::time_point &expire_time, worker &worker_ref, task_id tid) {
    timer_entry entry{expire_time, worker_ref, tid};
    auto id = entry.id();

    auto guard = timer_tree.lock();
    if (auto it = guard->find(entry.expire_seconds_since_epoch()); it != guard->end()) {
        auto &[_, area] = *it;
        area.entries.emplace(id, std::move(entry));
    } else {
        timer_area area{entry.expire_seconds_since_epoch()};
        area.entries.emplace(id, std::move(entry));
        guard->emplace(area.earliest_expire_time, std::move(area));
    }

    awake();

    return id;
}

void high_resolution_timer::unregister_timer(timer_id id) {
    auto expire_seconds =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::nanoseconds{id.expire_time_nanos})
            .count();

    if (auto g = timer_tree.lock(); g->contains(expire_seconds)) {
        auto &es = g->at(expire_seconds).entries;
        if (auto it = es.find(id); it != es.end()) {
            auto &e = it->second;
            e.worker_ref.activate_task(e.tid);
            es.erase(it);
        }
    }
}

};  // namespace asco::core::time
