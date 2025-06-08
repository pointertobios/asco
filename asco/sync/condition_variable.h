// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_SYNC_CONDITION_VARIABLE_H
#define ASCO_SYNC_CONDITION_VARIABLE_H 1

#include <deque>

#include <asco/future.h>
#include <asco/sync/spin.h>
#include <asco/utils/pubusing.h>

namespace asco::sync {

using task_id = core::sched::task::task_id;

template<core::runtime_type R = RT>
class condition_variable {
public:
    condition_variable() = default;

    void notify_one() {
        auto guard = waiting_tasks.lock();
        if (!guard->empty()) {
            auto id = std::move(guard->front());
            guard->pop_front();

            RT::get_runtime().awake(id);
        }
    }

    void notify_all() {
        auto guard = waiting_tasks.lock();
        for (auto id : *guard) { RT::get_runtime().awake(id); }
        guard->clear();
    }

    template<typename F>
        requires std::is_same_v<std::invoke_result_t<F>, bool>
    future_void_inline wait(F predicator) {
        auto this_id = this_coro::get_id();
        while (true) {
            with(auto guard = waiting_tasks.lock()) {
                if (predicator())
                    break;
                if (this_coro::aborted())
                    break;

                auto &worker = this_coro::get_worker();
                worker.sc.suspend(this_id);
                guard->push_back(this_id);
            }

            co_await std::suspend_always{};

            if (this_coro::aborted())
                std::erase_if(*waiting_tasks.lock(), [this_id](auto &id) { return id == this_id; });
        }
        co_return {};
    }

private:
    spin<std::deque<task_id>> waiting_tasks;
};

};  // namespace asco::sync

namespace asco {

using condition_variable = sync::condition_variable<RT>;

};  // namespace asco

#endif
