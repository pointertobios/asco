// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_SYNC_SEMAPHORE_H
#define ASCO_SYNC_SEMAPHORE_H 1

#include <coroutine>
#include <queue>

#include <asco/future.h>
#include <asco/futures.h>
#include <asco/suspend.h>
#include <asco/sync/spin.h>
#include <asco/utils/pubusing.h>

namespace asco {

template<size_t CounterMax, typename R = RT>
requires is_runtime<R>
class semaphore_base {
public:
    semaphore_base(size_t count)
        : counter(std::min(count, CounterMax)) {}
    
    size_t get_counter() {
        return counter.load(morder::relaxed);
    }

    void release(size_t update = 1) {
        if (update < 1)
            throw std::runtime_error(
                std::format("[ASCO] semaphore_base<{}>::release(): Cannot release for non-positive-integer.", CounterMax));

        if (counter.load(morder::relaxed) > CounterMax) {
            counter.store(CounterMax, morder::relaxed);
            return;
        }
        if (counter.load(morder::relaxed) >= CounterMax)
            return;

        size_t old_count;
        size_t new_count;

        do {

            old_count = counter.load(morder::relaxed);
            new_count = std::min(old_count + update, CounterMax);

        } while (!counter.compare_exchange_weak(
            old_count, new_count,
            morder::seq_cst,
            morder::relaxed
        ));

        const size_t awake_x = new_count - old_count;

        auto guard = waiting_tasks.lock();
        for (size_t i = 0; i < awake_x && !guard->empty(); ++i) {

            auto [id, worker] = std::move(guard->front());
            guard->pop();

            worker->sc.awake(id);
            worker->awake();

        }
    }

    [[nodiscard("[ASCO] semaphore_base<N>::acquire(): co_await or assign its return value.")]]
    future_void_inline acquire() {
        if (futures::aborted<future_void_inline>())
            co_return {};

        do {

            size_t val = counter.load(morder::acquire);
            if (val > 0) {
                if (counter.compare_exchange_strong(
                        val, val - 1,
                        morder::acq_rel, morder::relaxed))
                    break;
                continue;
            }

            if (counter.load(morder::acquire) > 0)
                continue;

            {
                auto guard = waiting_tasks.lock();

                if (counter.load(morder::acquire) == 0) {
                    auto worker = RT::__worker::get_worker();
                    auto id = worker->current_task_id();
                    worker->sc.suspend(id);

                    guard->push(std::make_pair(id, worker));
                } else {
                    continue;
                }
            }

            co_await std::suspend_always{};

        } while (true);

        if (futures::aborted<future_void_inline>())
            counter.fetch_add(1, morder::release); 

        co_return {};
    }

    bool try_acquire() {
        size_t val = counter.load(morder::acquire);

    try_once:
        if (val > 0) {
            if (counter.compare_exchange_strong(
                    val, val - 1,
                    morder::acq_rel, morder::relaxed))
                return true;
            goto try_once;
        }

        if (counter.load(morder::acquire) > 0)
            goto try_once;

        return false;
    }

private:
    atomic_size_t counter;
    spin<std::queue<std::pair<sched::task::task_id, worker *>>> waiting_tasks;
};

using binary_semaphore = semaphore_base<1>;

template<size_t MaxCount>
using semaphore = semaphore_base<MaxCount>;

};

#endif
