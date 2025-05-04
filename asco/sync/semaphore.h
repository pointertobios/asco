// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_SYNC_SEMAPHORE_H
#define ASCO_SYNC_SEMAPHORE_H 1

#include <coroutine>
#include <queue>

#include <asco/future.h>
#include <asco/futures.h>
#include <asco/sync/spin.h>
#include <asco/utils/pubusing.h>

namespace asco::sync {

using task_id = core::sched::task::task_id;

template<size_t CounterMax, typename R = RT>
    requires core::is_runtime<R>
class semaphore_base {
public:
    semaphore_base(size_t count)
            : counter(std::min(count, CounterMax)) {}

    size_t get_counter() { return counter.load(morder::relaxed); }

    void release(size_t update = 1) {
        if (update < 1)
            throw std::runtime_error(
                std::format(
                    "[ASCO] semaphore_base<{}>::release(): Cannot release for non-positive-integer.",
                    CounterMax));

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

        } while (!counter.compare_exchange_weak(old_count, new_count, morder::seq_cst, morder::relaxed));

        const size_t awake_x = new_count - old_count;

        auto guard = waiting_tasks.lock();
        for (size_t i = 0; i < awake_x && !guard->empty(); ++i) {
            auto [id, worker] = std::move(guard->front());
            guard->pop();

            RT::get_runtime().awake(id);
        }
    }

    [[nodiscard("[ASCO] semaphore_base<N>::acquire(): co_await or assign its return value.")]]
    future_void_inline acquire() {
        struct re {
            semaphore_base *self;
            int state{0};

            ~re() {
                if (!futures::aborted())
                    return;

                if (state == 1)
                    self->counter.fetch_add(1, morder::release);
            }
        } restorer{this};

        while (true) {
            if (futures::aborted()) {
                restorer.state = 0;
                co_return {};
            }

            size_t val = counter.load(morder::acquire);
            if (val > 0) {
                if (counter.compare_exchange_strong(val, val - 1, morder::acq_rel, morder::relaxed))
                    break;
                continue;
            }

            if (counter.load(morder::acquire) > 0)
                continue;

            {
                auto guard = waiting_tasks.lock();

                if (counter.load(morder::acquire) == 0) {
                    auto &worker = RT::__worker::get_worker();
                    auto id = worker.current_task_id();
                    worker.sc.suspend(id);

                    guard->push(std::make_pair(id, &worker));
                } else {
                    continue;
                }
            }

            co_await std::suspend_always{};
        }

        restorer.state = 1;
        if (futures::aborted()) {
            restorer.state = 0;
            counter.fetch_add(1, morder::release);
        }
        co_return {};
    }

    bool try_acquire() {
        size_t val = counter.load(morder::acquire);
        while (true) {
            if (val > 0) {
                if (counter.compare_exchange_strong(val, val - 1, morder::acq_rel, morder::relaxed))
                    return true;
                continue;
            }

            if (!counter.load(morder::acquire))
                return false;
        }
    }

private:
    atomic_size_t counter;
    spin<std::queue<std::pair<task_id, core::worker *>>> waiting_tasks;
};

using binary_semaphore = semaphore_base<1>;

template<size_t MaxCount>
using semaphore = semaphore_base<MaxCount>;

};  // namespace asco::sync

namespace asco {

using sync::binary_semaphore;
using sync::semaphore;

};  // namespace asco

#endif
