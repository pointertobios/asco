// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/time/timer.h>

#include <functional>

namespace asco::core::time {

timer_id timer_entry::gen_id(
    const std::chrono::high_resolution_clock::time_point &expire_time, const worker &worker_ptr,
    task_id tid) noexcept {
    auto expire =
        std::chrono::duration_cast<std::chrono::nanoseconds>(expire_time.time_since_epoch()).count();
    auto meta = std::hash<const worker *>{}(&worker_ptr) ^ std::hash<size_t>{}(tid);
    return {meta, static_cast<uint64_t>(expire)};
}

};  // namespace asco::core::time
