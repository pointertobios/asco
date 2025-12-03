// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <compare>
#include <map>

#include <asco/core/daemon.h>
#include <asco/core/time/timer.h>
#include <asco/core/worker.h>
#include <asco/sync/spin.h>
#include <asco/utils/types.h>

namespace asco::core::time {

using namespace types;

class high_resolution_timer final : public daemon {
public:
    high_resolution_timer();

    high_resolution_timer(const high_resolution_timer &) = delete;
    high_resolution_timer &operator=(const high_resolution_timer &) = delete;

    timer_id register_timer(
        const std::chrono::high_resolution_clock::time_point &expire_time, worker &worker_ptr,
        task_id task_id);

    void unregister_timer(timer_id id);

private:
    bool init() override;
    bool run_once(std::stop_token &st) override;
    void shutdown() override;

    struct timer_area {
        size_t earliest_expire_time;  // In seconds
        std::map<timer_id, timer_entry> entries{};

        std::strong_ordering operator<=>(const timer_area &other) const noexcept {
            return earliest_expire_time <=> other.earliest_expire_time;
        }

        std::strong_ordering operator<=>(const timer_entry &entry) const noexcept {
            return earliest_expire_time <=> entry.expire_seconds_since_epoch();
        }
    };

    spin<std::map<size_t, timer_area>> timer_tree;
};

};  // namespace asco::core::time
