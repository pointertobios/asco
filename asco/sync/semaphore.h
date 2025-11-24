// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>

#include <asco/core/runtime.h>
#include <asco/core/wait_queue.h>
#include <asco/core/worker.h>
#include <asco/future.h>
#include <asco/utils/concepts.h>
#include <asco/utils/types.h>

namespace asco::sync {

using namespace types;
using namespace concepts;

using std::chrono::duration_cast;
using std::chrono::nanoseconds;
using clock = std::chrono::high_resolution_clock;

template<size_t CountMax>
class semaphore_base {
public:
    explicit semaphore_base(size_t initial_count) noexcept
            : count{initial_count} {}

    bool try_acquire() noexcept {
        size_t old_count = count.load(morder::acquire);
        if (!old_count) {
            return false;
        }
        return count.compare_exchange_strong(old_count, old_count - 1, morder::acquire, morder::relaxed);
    }

    future<void> acquire() {
        size_t old_count, new_count;
        do {
        fetch_count:
            old_count = count.load(morder::acquire);
            if (!old_count) {
                co_await wq.wait();
                goto fetch_count;
            }
            new_count = old_count - 1;
        } while (!count.compare_exchange_weak(old_count, new_count, morder::acquire, morder::relaxed));
        co_return;
    }

    future<bool> acquire_for(const duration_type auto &timeout) {
        auto expire_time = clock::now() + duration_cast<nanoseconds>(timeout);
        size_t old_count, new_count;
        do {
        fetch_count:
            old_count = count.load(morder::acquire);
            if (!old_count) {
                auto &w = core::worker::this_worker();
                auto &timer = core::runtime::this_runtime().timer();
                auto tid = timer.register_timer(expire_time, w, w.current_task());
                auto it = co_await wq.wait();
                if (it && clock::now() >= expire_time) {
                    wq.interrupt_wait(*it);
                    co_return false;
                } else {
                    timer.unregister_timer(tid);
                    goto fetch_count;
                }
            }
            new_count = old_count - 1;
        } while (!count.compare_exchange_weak(old_count, new_count, morder::acquire, morder::relaxed));
        co_return true;
    }

    future<bool> acquire_until(const time_point_type auto &expire_time) {
        size_t old_count, new_count;
        do {
        fetch_count:
            old_count = count.load(morder::acquire);
            if (!old_count) {
                auto &w = core::worker::this_worker();
                auto &timer = core::runtime::this_runtime().timer();
                auto tid = timer.register_timer(expire_time, w, w.current_task());
                auto it = co_await wq.wait();
                if (it && clock::now() >= expire_time) {
                    wq.interrupt_wait(*it);
                    co_return false;
                } else {
                    timer.unregister_timer(tid);
                    goto fetch_count;
                }
            }
            new_count = old_count - 1;
        } while (!count.compare_exchange_weak(old_count, new_count, morder::acquire, morder::relaxed));
        co_return true;
    }

    void release(size_t update = 1) {
        size_t old_count, new_count;
        size_t inc;
        do {
            old_count = count.load(morder::acquire);
            inc = std::min(update, CountMax - old_count);
            new_count = old_count + inc;
        } while (!count.compare_exchange_weak(old_count, new_count, morder::release, morder::relaxed));
        wq.notify(inc);
    }

private:
    atomic_size_t count;
    core::wait_queue wq;
};

};  // namespace asco::sync

namespace asco {

using binary_semaphore = sync::semaphore_base<1>;
template<types::size_t CountMax>
using counting_semaphore = sync::semaphore_base<CountMax>;
using unlimited_semaphore = sync::semaphore_base<std::numeric_limits<types::size_t>::max()>;

};  // namespace asco
