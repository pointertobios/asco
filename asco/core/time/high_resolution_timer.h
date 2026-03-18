// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <coroutine>
#include <map>
#include <optional>
#include <unordered_set>

#include <asco/core/time/timer.h>
#include <asco/sync/spinlock.h>
#include <asco/sync/spinrwlock.h>

namespace asco::core::time {

class high_resolution_timer final : public timer {
public:
    high_resolution_timer();

    high_resolution_timer(const high_resolution_timer &) = delete;
    high_resolution_timer &operator=(const high_resolution_timer &) = delete;

    high_resolution_timer(high_resolution_timer &&) = delete;
    high_resolution_timer &operator=(high_resolution_timer &&) = delete;

    std::optional<timer_id>
    register_timer(std::chrono::steady_clock::time_point time_point, std::coroutine_handle<> handle) override;

    void cancel_timer(timer_id tmid) override;

    bool is_expired(const timer_id &tmid) const override;

private:
    bool run_once(std::stop_token &st) override;

    struct entry_area {
        std::size_t seconds_from_epoch;
        mutable sync::spinlock<std::map<timer_id, timer_entry>> entries{};

        mutable sync::spinlock<std::unordered_set<timer_id>> removed_entries;
    };

    sync::spinrwlock<std::map<std::size_t, entry_area>> m_timer_tree;
};

};  // namespace asco::core::time
