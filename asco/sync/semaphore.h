// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <atomic>
#include <coroutine>
#include <limits>

#include <asco/concurrency/concurrency.h>
#include <asco/core/worker.h>
#include <asco/sync/spinlock.h>
#include <asco/util/raw_storage.h>
#include <asco/yield.h>

namespace asco::sync {

template<std::size_t N>
    requires(N > 0)
class semaphore final {
public:
    semaphore(std::size_t count)
            : m_count{std::min(N, count)} {}

    semaphore(const semaphore &) = delete;
    semaphore &operator=(const semaphore &) = delete;

    semaphore(semaphore &&) = delete;
    semaphore &operator=(semaphore &&) = delete;

    bool try_acquire() {
        std::size_t oldc;
        do {
            oldc = m_count.load(std::memory_order::acquire);
            if (oldc == 0) {
                return false;
            }
        } while (!m_count.compare_exchange_weak(
            oldc, oldc - 1, std::memory_order::acq_rel, std::memory_order::relaxed));
        return true;
    }

    future<void> acquire() {
        std::size_t oldc;
        std::size_t i{std::numeric_limits<std::size_t>::max()};
        do {
        fetch:
            oldc = m_count.load(std::memory_order::acquire);
            if (oldc == 0) {
                ++i;
                if (i <= 100) {
                } else if (i <= 1000) {
                    concurrency::exp_withdraw(i);
                } else if (i <= 2000) {
                    co_await this_task::yield();
                } else {
                    auto &w = core::worker::current();
                    auto h = w.this_coroutine();
                    m_wait_queue.lock()->push_back(h);
                    w.suspend_current_handle(h);
                    co_await this_task::yield();
                    i = std::numeric_limits<std::size_t>::max();
                }
                goto fetch;
            }
        } while (!m_count.compare_exchange_weak(
            oldc, oldc - 1, std::memory_order::acq_rel, std::memory_order::relaxed));
    }

    std::size_t release(std::size_t n = 1) {
        auto g = m_wait_queue.lock();

        std::size_t oldc;
        std::size_t diff;
        do {
            oldc = m_count.load(std::memory_order::acquire);
            diff = std::min(n, N - oldc);
        } while (!m_count.compare_exchange_weak(
            oldc, oldc + diff, std::memory_order::acq_rel, std::memory_order::relaxed));

        auto x = diff;
        while (!g->empty() && x--) {
            auto h = g->front();
            g->pop_front();
            core::worker::of_handle(h).awake_handle(h);
        }

        return diff;
    }

    std::size_t get_count() const { return m_count.load(std::memory_order::acquire); }

private:
    std::atomic_size_t m_count;
    spinlock<std::deque<std::coroutine_handle<>>> m_wait_queue;
};

using binary_semaphore = semaphore<1>;
using unlimited_semaphore = semaphore<std::numeric_limits<std::size_t>::max()>;

};  // namespace asco::sync
