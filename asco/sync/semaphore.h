// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

#include <asco/concurrency/concurrency.h>
#include <asco/core/runtime.h>
#include <asco/panic.h>
#include <asco/sync/condition_variable.h>
#include <asco/this_task.h>
#include <asco/yield.h>

namespace asco::sync {

template<std::size_t N>
    requires(N > 0)
class semaphore final {
    using counter_type =
        std::conditional_t<N <= std::numeric_limits<std::uint8_t>::max(), std::uint8_t, std::size_t>;

public:
    semaphore(std::size_t count)
            : m_count{std::min(N, count)} {}

    semaphore(const semaphore &) = delete;
    semaphore &operator=(const semaphore &) = delete;

    semaphore(semaphore &&) = delete;
    semaphore &operator=(semaphore &&) = delete;

    bool try_acquire() {
        counter_type oldc;
        do {
            oldc = m_count.load(std::memory_order::acquire);
            if (oldc == 0) {
                return false;
            }
        } while (!m_count.compare_exchange_weak(
            oldc, oldc - 1, std::memory_order::acq_rel, std::memory_order::relaxed));
        return true;
    }

    void blocking_acquire() {
        if (!this_task::is_blocking_env()) [[unlikely]] {
            panic("asco::sync::semaphore: 在异步任务中禁止使用同步阻塞调用");
        }
        core::runtime::current().block_on([this]() -> future<void> { co_await acquire(); });
    }

    future<void> acquire() {
        counter_type oldc;
        std::size_t i{std::numeric_limits<std::size_t>::max()};
        do {
        fetch:
            oldc = m_count.load(std::memory_order::acquire);
            if (oldc == 0) {
                ++i;
                if (i <= 10) {
                } else if (i <= 16) {
                    concurrency::exp_withdraw(i - 10);
                } else if (i <= 100) {
                    co_await this_task::yield();
                } else {
                    if (co_await m_cv.wait_once([this, &oldc] {
                            return (oldc = m_count.load(std::memory_order::acquire)) != 0;
                        })) {
                        i = std::numeric_limits<std::size_t>::max();
                        goto fetch;
                    }
                    i = std::numeric_limits<std::size_t>::max();
                }
                goto fetch;
            }
        } while (!m_count.compare_exchange_weak(
            oldc, oldc - 1, std::memory_order::acq_rel, std::memory_order::relaxed));
    }

    std::size_t release(std::size_t n) {
        while (true) {
            counter_type oldc = m_count.load(std::memory_order::acquire);
            auto diff = static_cast<counter_type>(std::min<std::size_t>(n, N - oldc));
            auto res = m_cv.notify(
                [this, &oldc, diff] {
                    return m_count.compare_exchange_weak(
                        oldc, oldc + diff, std::memory_order::acq_rel, std::memory_order::relaxed);
                },
                static_cast<std::size_t>(diff));
            if (res) {
                return diff;
            }
            switch (res.error()) {
            case notify_failed::no_waiters: {
                if (!m_count.compare_exchange_weak(
                        oldc, oldc + diff, std::memory_order::acq_rel, std::memory_order::relaxed)) {
                    continue;
                }
                return diff;
            } break;
            case notify_failed::predicate_false: {
            } break;
            }
        }
    }

    std::size_t release() { return release(1); }

    counter_type get_count() const { return m_count.load(std::memory_order::acquire); }

private:
    std::atomic<counter_type> m_count;
    condition_variable m_cv;
};

using binary_semaphore = semaphore<1>;
using unlimited_semaphore = semaphore<std::numeric_limits<std::size_t>::max()>;

};  // namespace asco::sync
