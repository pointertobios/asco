// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <functional>

#include <asco/future.h>
#include <asco/yield.h>

namespace asco::test {

template<typename Pred>
future<bool> wait_until(Pred &&pred, std::chrono::steady_clock::duration max_wait = std::chrono::seconds{2}) {
    const auto deadline = std::chrono::steady_clock::now() + max_wait;
    while (std::chrono::steady_clock::now() < deadline) {
        if (std::invoke(pred)) {
            co_return true;
        }
        co_await this_task::yield();
    }
    co_return std::invoke(pred);
}

template<typename Pred>
future<bool>
stays_false_for(Pred &&pred, std::chrono::steady_clock::duration duration = std::chrono::milliseconds{50}) {
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline) {
        if (std::invoke(pred)) {
            co_return false;
        }
        co_await this_task::yield();
    }
    co_return !std::invoke(pred);
}

};  // namespace asco::test