// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_SYNC_SEMAPHORE_H
#define ASCO_SYNC_SEMAPHORE_H 1

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
    using task_handle = RT::scheduler::task_handle;

    semaphore_base(size_t count)
        : counter(std::min(count, CounterMax)) {}
    
    size_t get_counter() {
        return counter.load(morder::relaxed);
    }

    void release(size_t update = 1) {
        if (counter.load(morder::relaxed) == CounterMax)
            return;

        size_t c = counter.load(morder::acquire);
        while (true) if (counter.compare_exchange_strong(
                c, std::min(c + update, CounterMax),
                morder::release, morder::relaxed)) {

            auto [h, worker] = ({
                auto guard = waiting_tasks.lock();
                if (guard->empty())
                    return;
                auto res = std::move(guard->front());
                guard->pop();
                res;
            });

            h.put_back();
            worker->awake();
            return;
        }
    }

    future_void_inline acquire() {
        size_t c;

    try_acquire:
        c = counter.load(morder::acquire);
        if (c > 0 && counter.compare_exchange_strong(
                c, c - 1,
                morder::acquire, morder::relaxed)) {

            if (futures::aborted<future_void_inline>())
                counter.store(c, morder::release);

            co_return {};
        } else {
            auto worker = RT::__worker::get_worker();
            auto taskh = worker->sc.give_out(worker->current_task().id);
            waiting_tasks.lock()->push(std::make_pair(taskh, worker));

            co_await suspend{};

            if (futures::aborted<future_void_inline>())
                co_return {};

            goto try_acquire;
        }
    }

private:
    atomic_size_t counter;
    spin<std::queue<std::pair<task_handle, worker *>>> waiting_tasks;
};

using binary_semaphore = semaphore_base<1>;

template<size_t MaxCount>
using semaphore = semaphore_base<MaxCount>;

};

#endif
