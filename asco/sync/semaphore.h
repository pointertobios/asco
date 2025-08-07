// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_SYNC_SEMAPHORE_H
#define ASCO_SYNC_SEMAPHORE_H 1

#include <coroutine>
#include <deque>

#include <asco/future.h>
#include <asco/sync/spin.h>
#include <asco/utils/pubusing.h>

namespace asco::sync {

using task_id = core::sched::task::task_id;

template<size_t CounterMax>
class semaphore_base {
public:
    semaphore_base(size_t count)
            : counter(std::min(count, CounterMax)) {}

    semaphore_base(const semaphore_base &) = delete;
    semaphore_base(semaphore_base &&) = delete;

    size_t get_counter() { return counter.load(morder::relaxed); }

    void release(size_t update = 1) {
        if (update < 1)
            throw asco::runtime_error(
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
            auto id = std::move(guard->front());
            guard->pop_front();

            RT::get_runtime().awake(id);
        }
    }

    [[nodiscard("[ASCO] semaphore_base<N>::acquire(): co_await or assign its return value.")]]
    future_inline<void> acquire() {
        struct re {
            semaphore_base *self;
            int state{0};

            ~re() {
                if (!this_coro::aborted())
                    return;

                this_coro::throw_coroutine_abort<future_inline<void>>();

                {
                    auto guard = self->waiting_tasks.lock();
                    auto id = this_coro::get_id();
                    std::erase_if(*guard, [id](auto &_id) { return _id == id; });
                }

                if (state == 1)
                    self->counter.fetch_add(1, morder::release);
            }
        } restorer{this};

        auto this_id = this_coro::get_id();

        while (true) {
            if (this_coro::aborted()) {
                restorer.state = 0;
                throw coroutine_abort{};
            }

            size_t val = counter.load(morder::acquire);
            if (val > 0) {
                if (counter.compare_exchange_strong(val, val - 1, morder::acq_rel, morder::relaxed))
                    break;
                continue;
            }

            if (counter.load(morder::acquire) > 0)
                continue;

            with(auto guard = waiting_tasks.lock()) {
                if (counter.load(morder::acquire) == 0) {
                    auto &worker = this_coro::get_worker();
                    worker.sc.suspend(this_id);
                    guard->push_back(this_id);
                } else {
                    continue;
                }
            }

            co_await std::suspend_always{};
            break;
        }

        restorer.state = 1;
        if (this_coro::aborted()) {
            restorer.state = 0;
            counter.fetch_add(1, morder::release);
            throw coroutine_abort{};
        }
        co_return;
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
    spin<std::deque<task_id>> waiting_tasks;
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
