// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/time/high_resolution_timer.h>

#include <asco/core/worker.h>
#include <asco/util/type_id.h>

namespace asco::core::time {

namespace detail {

std::uint64_t sec_from_epoch(std::chrono::steady_clock::time_point tp) {
    return std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count();
}

};  // namespace detail

high_resolution_timer::high_resolution_timer()
        : timer{this} {
    auto _ = daemon::start();
}

std::optional<high_resolution_timer::timer_id>
high_resolution_timer::register_timer(std::chrono::steady_clock::time_point time_point, awake_token token) {
    if (time_point < std::chrono::steady_clock::now()) {
        return std::nullopt;
    }

    timer_id tmid{static_cast<std::uint64_t>(time_point.time_since_epoch().count()), token.hash()};
    auto seconds_from_epoch = detail::sec_from_epoch(time_point);
    if (auto g = m_timer_tree.write()) {
        if (!g->contains(seconds_from_epoch)) {
            // 由于 sync::spinlock 不可移动、复制，第二个参数实际上会在哈希表中原地构造一个
            // entry_area{seconds_from_epoch, {}}
            g->emplace(seconds_from_epoch, seconds_from_epoch);
        }
        g->at(seconds_from_epoch).entries.lock()->emplace(tmid, timer_entry{time_point, std::move(token)});
    }

    awake();

    return tmid;
}

void high_resolution_timer::cancel_timer(timer_id tmid) {
    if (auto g = m_timer_tree.read()) {
        auto seconds_from_epoch = detail::sec_from_epoch(
            std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(tmid.seq)));
        if (g->contains(seconds_from_epoch)) {
            auto &area = g->at(seconds_from_epoch);
            if (auto gg = area.removed_entries.lock()) {
                gg->insert(tmid);
            }
            awake();
        }
    }
}

bool high_resolution_timer::is_expired(const timer_id &tmid) const {
    if (auto g = m_timer_tree.read()) {
        auto seconds_from_epoch = detail::sec_from_epoch(
            std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(tmid.seq)));
        if (g->contains(seconds_from_epoch)) {
            auto &area = g->at(seconds_from_epoch);
            bool a = area.entries.lock()->contains(tmid);
            bool b = area.removed_entries.lock()->contains(tmid);
            return !a || b;
        } else {
            return true;
        }
    }
    return false;
}

bool high_resolution_timer::run_once(std::stop_token &st) {
    if (st.stop_requested()) {
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    auto seconds_from_epoch = detail::sec_from_epoch(now);
    do {
        if (auto tree_guard = m_timer_tree.read(); !tree_guard->empty()) {
            auto &area = *tree_guard->begin();
            if (area.first > seconds_from_epoch) {
                break;
            }

            auto entries_guard = area.second.entries.lock();
            auto removed_entries_guard = area.second.removed_entries.lock();
            for (auto it = entries_guard->begin(); it != entries_guard->end();) {
                if (removed_entries_guard->contains(it->first)) {
                    removed_entries_guard->erase(it->first);
                    it = entries_guard->erase(it);
                    continue;
                }

                if (it->second.time_point > now) {
                    break;
                }

                it->second.token.awake();
                it = entries_guard->erase(it);
            }
        }
    } while (false);

    if (auto g = m_timer_tree.write()) {
        while (!g->empty() && g->begin()->second.entries.lock()->empty()) {
            g->erase(g->begin());
        }
    }

    if (st.stop_requested() && m_timer_tree.read()->empty()) {
        return false;
    }

    std::optional<std::chrono::steady_clock::time_point> next_time_point;
    if (auto g = m_timer_tree.read(); g->empty()) {
        next_time_point = std::nullopt;
    } else if (auto gg = g->begin()->second.entries.lock(); gg->empty()) {
        next_time_point = std::nullopt;
    } else {
        auto tp = gg->begin()->second.time_point;
        next_time_point = tp;
    }

    if (next_time_point) {
        sleep_until_awake_before(*next_time_point);
    } else {
        sleep_until_awake();
    }

    return true;
}

};  // namespace asco::core::time
