// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <coroutine>
#include <cstddef>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <asco/concurrency/ring_queue.h>
#include <asco/core/worker.h>
#include <asco/future.h>
#include <asco/invoke.h>
#include <asco/join_handle.h>
#include <asco/panic.h>
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

    runtime(runtime &&) = delete;
    runtime &operator=(runtime &&) = delete;

    static runtime &current();

    auto block_on(async_function<> auto &&fn) {
        asco_assert(!in_runtime());

        using return_type = std::invoke_result_t<decltype(fn)>::output_type;

        auto jh = spawn_impl(std::forward<decltype(fn)>(fn));

        m_backsem_sync->acquire();
        m_coroutine_tx.try_send({jh.m_state->this_handle, &jh.get_cancel_source()});
        awake_next();
        if constexpr (std::is_void_v<return_type>) {
            jh.await();
            return;
        } else {
            return jh.await();
        }
    }

    auto spawn(async_function<> auto &&fn) {
        using output_type = std::invoke_result_t<decltype(fn)>::output_type;

        auto jh = spawn_impl(std::forward<decltype(fn)>(fn));

        if (in_runtime()) {
            if (!m_backsem_sync->try_acquire()) {
                worker::current().fetch_task();
            }
        } else {
            m_backsem_sync->acquire();
        }
        m_coroutine_tx.try_send({jh.m_state->this_handle, &jh.get_cancel_source()});
        awake_next();

        return jh;
    }

    auto spawn_blocking(std::invocable<> auto &&fn)
        requires(!async_function<decltype(fn)>)
    {
        using output_type = std::invoke_result_t<decltype(fn)>;

        return spawn([fn = std::forward<decltype(fn)>(fn)]() -> future<output_type> {
            if constexpr (std::is_void_v<output_type>) {
                std::invoke(std::forward<decltype(fn)>(fn));
                co_return;
            } else {
                co_return std::invoke(std::forward<decltype(fn)>(fn));
            }
        });
    }

private:
    void awake_next() noexcept;

    auto spawn_impl(async_function<> auto fn)
        -> join_handle<typename std::invoke_result_t<decltype(fn)>::output_type> {
        using output_type = std::invoke_result_t<decltype(fn)>::output_type;
        auto coro = co_invoke(fn);
        if constexpr (std::is_void_v<output_type>) {
            co_await coro;
            co_return;
        } else {
            co_return co_await coro;
        }
    }

    detail::idle_workers_receiver m_idle_workers_rx;

    detail::coroutine_sender m_coroutine_tx;
    std::shared_ptr<std::counting_semaphore<detail::coroutine_queue_capacity>> m_backsem_sync;

    std::vector<std::unique_ptr<worker>> m_workers;
    std::vector<runtime **> m_workers_local_runtime_ptr;

    inline static sync::spinlock<std::unordered_map<std::coroutine_handle<>, worker *>>
        m_corohandle_worker_map;

    inline thread_local static runtime *_current_runtime{nullptr};
};

};  // namespace core

auto spawn(async_function<> auto &&fn) {
    return core::runtime::current().spawn(std::forward<decltype(fn)>(fn));
}

auto spawn_blocking(std::invocable<> auto &&fn)
    requires(!async_function<decltype(fn)>)
{
    return core::runtime::current().spawn_blocking(std::forward<decltype(fn)>(fn));
}

};  // namespace asco
