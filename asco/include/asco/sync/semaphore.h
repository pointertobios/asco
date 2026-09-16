// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <limits>

#include "asco/sync/condition_variable.h"
#include "asco/types/int.h"

namespace asco::sync {

template<usize N = std::numeric_limits<usize>::max()>
class counting_semaphore final {
public:
    counting_semaphore() = delete;

    counting_semaphore(usize x)
            : m_counter{x} {
        ASCO_ASSERT(x < N);
    }

    counting_semaphore(const counting_semaphore &) = delete;
    counting_semaphore &operator=(const counting_semaphore &) = delete;

    counting_semaphore(counting_semaphore &&) = delete;
    counting_semaphore &operator=(counting_semaphore &&) = delete;

    future<> acquire() {
        while (true) {
            auto c = m_counter.load(morder::acquire);
            if (c == 0) {
                co_await m_cv([&counter = m_counter] { return counter > 0; });
                continue;
            }
            if (m_counter.compare_exchange_weak(c, c - 1, morder::acq_rel, morder::relaxed)) {
                co_return;
            }
        }
    }

    void release(usize n = 1) noexcept {
        (void)m_counter.fetch_add(n);
        while (n--) {
            m_cv.notify_one();
        }
    }

private:
    condition_variable m_cv{};
    std::atomic<usize> m_counter;
};

using binary_semaphore = counting_semaphore<1>;

};  // namespace asco::sync
