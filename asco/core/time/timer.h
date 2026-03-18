// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <compare>
#include <coroutine>
#include <limits>
#include <optional>

#include <asco/core/daemon.h>
#include <asco/util/murmur.h>
#include <asco/util/type_id.h>

namespace asco::core::time {

class timer : public daemon {
public:
    struct timer_entry {
        std::chrono::steady_clock::time_point time_point;
        std::coroutine_handle<> handle;
    };

    struct timer_id {
        std::uint64_t seq{std::numeric_limits<std::uint64_t>::max()};
        std::uint64_t hash{std::numeric_limits<std::uint64_t>::max()};

        std::strong_ordering operator<=>(const timer_id &rhs) const noexcept {
            if (auto cmp = seq <=> rhs.seq; cmp != std::strong_ordering::equal) {
                return cmp;
            }
            return hash <=> rhs.hash;
        }

        bool operator==(const timer_id &rhs) const noexcept = default;
    };

    // 注册一个定时器，time_point 早于当前时间时返回无效的 timer_id
    virtual std::optional<timer_id>
    register_timer(std::chrono::steady_clock::time_point time_point, std::coroutine_handle<> handle) = 0;

    // 取消定时器，tmid 无效或定时器已过期时无任何效果
    virtual void cancel_timer(timer_id tmid) = 0;

    virtual bool is_expired(const timer_id &tmid) const = 0;

protected:
    template<typename T>
    timer(T *)
            : daemon("asco-timer")
            , m_timer_type{util::type_id::of<T>()} {}

    util::type_id m_timer_type{util::type_id::of<timer>()};
};

};  // namespace asco::core::time

template<>
class std::hash<asco::core::time::timer::timer_id> {
public:
    std::size_t operator()(const asco::core::time::timer::timer_id &tmid) const noexcept {
        return asco::util::detail::mix64(tmid.seq ^ tmid.hash);
    }
};
