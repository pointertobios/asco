// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cassert>
#include <cstddef>
#include <deque>
#include <type_traits>
#include <vector>

#include <asco/concurrency/ring_queue.h>
#include <asco/core/worker.h>
#include <asco/future.h>
#include <asco/invoke.h>
#include <asco/join_handle.h>
#include <asco/util/types.h>

namespace asco {

bool in_runtime() noexcept;

namespace core {

class runtime final {
    friend class worker;
    friend bool asco::in_runtime() noexcept;

public:
    explicit runtime(std::size_t nthreads = 0);

    runtime(const runtime &) = delete;
    runtime &operator=(const runtime &) = delete;

    runtime(runtime &&);
    runtime &operator=(runtime &&);

    static runtime &current();

    template<typename T>
    T block_on(future<T> fut) {
        assert(!in_runtime());

        auto jh = co_invoke([&fut] -> join_handle<T> {
            auto coro = std::move(fut);
            if constexpr (std::is_void_v<T>) {
                co_await coro;
                co_return;
            } else {
                co_return co_await coro;
            }
        });

        m_backsem_sync->acquire();
        m_coroutine_tx.try_send(jh.m_state->this_handle);
        awake_next();
        if constexpr (std::is_void_v<T>) {
            jh.await();
            return;
        } else {
            return jh.await();
        }
    }

    auto block_on(async_function<> auto &&fn) -> std::invoke_result_t<decltype(fn)>::output_type {
        return block_on(co_invoke(std::forward<decltype(fn)>(fn)));
    }

    template<util::types::move_secure T>
    join_handle<T> spawn(future<T> fut) {
        auto jh = co_invoke([&fut] -> join_handle<T> {
            auto coro = std::move(fut);
            if constexpr (std::is_void_v<T>) {
                co_await coro;
                co_return;
            } else {
                co_return co_await coro;
            }
        });

        if (in_runtime()) {
            if (!m_backsem_sync->try_acquire()) {
                worker::current().fetch_task();
            }
        } else {
            m_backsem_sync->acquire();
        }
        m_coroutine_tx.try_send(jh.m_state->this_handle);
        awake_next();

        return jh;
    }

    auto spawn(async_function<> auto &&fn)
        -> join_handle<typename std::invoke_result_t<decltype(fn)>::output_type> {
        return spawn(co_invoke(std::forward<decltype(fn)>(fn)));
    }

private:
    void awake_next() noexcept;

    std::deque<std::size_t> m_idle_workers;

    detail::coroutine_sender m_coroutine_tx;
    std::shared_ptr<std::counting_semaphore<detail::coroutine_queue_capacity>> m_backsem_sync;

    std::vector<std::unique_ptr<worker>> m_workers;
    std::vector<runtime **> m_workers_local_runtime_ptr;

    inline thread_local static runtime *_current_runtime{nullptr};
};

};  // namespace core

};  // namespace asco
