// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <bit>

#include <asco/core/worker.h>
#include <asco/utils/types.h>

namespace asco::core::time {

using namespace types;

struct alignas(sizeof(uint128_t)) timer_id {
    const uint64_t meta;
    const uint64_t expire_time_nanos;

    asco_always_inline std::strong_ordering operator<=>(const timer_id &other) const noexcept {
        if constexpr (std::endian::native == std::endian::little) {
            return *reinterpret_cast<const uint128_t *>(this)
                   <=> *reinterpret_cast<const uint128_t *>(&other);
        } else {
            return (static_cast<uint128_t>(expire_time_nanos) << 64 | meta)
                   <=> (static_cast<uint128_t>(other.expire_time_nanos) << 64 | other.meta);
        }
    }
};

struct timer_entry {
    const std::chrono::high_resolution_clock::time_point expire_time;
    worker &worker_ref;
    const task_id tid;
    const timer_id _id{gen_id(expire_time, worker_ref, tid)};
    const size_t _expire_seconds_since_epoch{static_cast<size_t>(
        std::chrono::duration_cast<std::chrono::seconds>(expire_time.time_since_epoch()).count())};

    asco_always_inline std::strong_ordering operator<=>(const timer_entry &other) const noexcept {
        return expire_time <=> other.expire_time;
    }

    asco_always_inline timer_id id() const noexcept { return _id; }

    asco_always_inline size_t expire_seconds_since_epoch() const noexcept {
        return _expire_seconds_since_epoch;
    }

private:
    static timer_id gen_id(
        const std::chrono::high_resolution_clock::time_point &expire_time, const worker &worker_ptr,
        task_id tid) noexcept;
};

template<typename T>
concept timer = requires(T t) {
    T();
    {
        t.register_timer(
            std::declval<const std::chrono::high_resolution_clock::time_point &>(), std::declval<worker &>(),
            std::declval<task_id>())
    } -> std::same_as<timer_id>;
    { t.unregister_timer(std::declval<timer_id>()) } -> std::same_as<void>;
};

};  // namespace asco::core::time
