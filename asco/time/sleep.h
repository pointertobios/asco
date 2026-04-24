// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>

#include <asco/future.h>
#include <asco/util/types.h>

namespace asco::time {

future<void> sleep_until(std::chrono::steady_clock::time_point time_point);

future<void> sleep_for(util::types::duration_type auto &&duration) {
    auto expire = std::chrono::steady_clock::now() + duration;
    return sleep_until(expire);
}

};  // namespace asco::time
