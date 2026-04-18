// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <coroutine>
#include <cstddef>
#include <type_traits>
#include <vector>

#include <asco/concurrency/hash_map.h>
#include <asco/concurrency/ring_queue.h>
#include <asco/core/time/high_resolution_timer.h>
#include <asco/core/time/timer.h>
#include <asco/core/worker.h>
#include <asco/future.h>
#include <asco/invoke.h>
#include <asco/join_handle.h>
#include <asco/panic.h>
#include <asco/util/safe_erased.h>
#include <asco/util/types.h>

namespace asco {

bool in_runtime() noexcept;

namespace core {

class runtime;

class runtime_builder {
    friend class runtime;

public:
    static runtime_builder multi_threaded(std::size_t nthreads = 0) { return runtime_builder{nthreads}; }

    static runtime_builder single_threaded() { return runtime_builder{1}; }

    runtime_builder &&
    with_timer(std::unique_ptr<time::timer> timer = std::make_unique<time::high_resolution_timer>()) && {
        m_timer = std::move(timer);
        return std::move(*this);
    }

    runtime build() &&;

private:
    explicit runtime_builder(std::size_t nthreads)
            : m_nthreads(nthreads) {}

    std::size_t m_nthreads{0};
    std::unique_ptr<time::timer> m_timer{nullptr};
};

class runtime final {
    friend class worker;
    friend bool asco::in_runtime() noexcept;

public:
    explicit runtime(runtime_builder &&builder);

    runtime(const runtime &) = delete;
    runtime &operator=(const runtime &) = delete;

    runtime(runtime &&) = delete;
    runtime &operator=(runtime &&) = delete;

    static runtime &current();

    time::timer &get_timer();

    template<typename TaskLocalStorage>
    auto block_on(async_function<> auto &&fn, TaskLocalStorage &&task_local_storage) {
        asco_assert(this_task::is_blocking_env());

        using return_type = std::invoke_result_t<decltype(fn)>::output_type;

        auto jh = spawn_impl<TaskLocalStorage>(std::forward<decltype(fn)>(fn));
        util::safe_erased tls;
        if constexpr (!std::is_void_v<TaskLocalStorage>) {
            tls = jh.initialize_task_local_storage(
                std::forward<decltype(task_local_storage)>(task_local_storage));
        } else {
            tls = util::safe_erased::of_void();
        }

        m_backsem_sync->acquire();
        m_coroutine_tx.try_send(
            {jh.m_state->this_handle, &jh.m_state->cancel_awake_token,
             &jh.m_state->__cancel_awake_token_storage, &jh.get_cancel_source(), std::move(tls), false});
        awake_next();
        if constexpr (std::is_void_v<return_type>) {
            jh.await();
            return;
        } else {
            return jh.await();
        }
    }

    auto block_on(async_function<> auto &&fn) {
        return block_on(std::forward<decltype(fn)>(fn), std::monostate{});
    }

    template<typename TaskLocalStorage>
    auto spawn(async_function<> auto &&fn, TaskLocalStorage &&task_local_storage) {
        using output_type = std::invoke_result_t<decltype(fn)>::output_type;

        auto jh = spawn_impl<TaskLocalStorage>(std::forward<decltype(fn)>(fn));
        util::safe_erased tls;
        if constexpr (!std::is_void_v<TaskLocalStorage>) {
            tls = jh.initialize_task_local_storage(
                std::forward<decltype(task_local_storage)>(task_local_storage));
        } else {
            tls = util::safe_erased::of_void();
        }

        if (in_runtime()) {
            if (!m_backsem_sync->try_acquire()) {
                worker::current().fetch_task();
            }
        } else {
            m_backsem_sync->acquire();
        }
        m_coroutine_tx.try_send(
            {jh.m_state->this_handle, &jh.m_state->cancel_awake_token,
             &jh.m_state->__cancel_awake_token_storage, &jh.get_cancel_source(), std::move(tls), false});
        awake_next();

        return jh;
    }

    auto spawn(async_function<> auto &&fn) { return spawn(std::forward<decltype(fn)>(fn), std::monostate{}); }

    template<typename TaskLocalStorage>
    auto spawn_blocking(std::invocable<> auto &&fn, TaskLocalStorage &&task_local_storage)
        requires(!async_function<decltype(fn)>)
    {
        using output_type = std::invoke_result_t<decltype(fn)>;

        auto jh = spawn_blocking_impl<TaskLocalStorage>(std::forward<decltype(fn)>(fn));
        util::safe_erased tls;
        if constexpr (!std::is_void_v<TaskLocalStorage>) {
            tls = jh.initialize_task_local_storage(
                std::forward<decltype(task_local_storage)>(task_local_storage));
        } else {
            tls = util::safe_erased::of_void();
        }

        if (in_runtime()) {
            if (!m_backsem_sync->try_acquire()) {
                worker::current().fetch_task();
            }
        } else {
            m_backsem_sync->acquire();
        }
        m_coroutine_tx.try_send(
            {jh.m_state->this_handle, &jh.m_state->cancel_awake_token,
             &jh.m_state->__cancel_awake_token_storage, &jh.get_cancel_source(), std::move(tls), true});
        awake_next();

        return jh;
    }

    auto spawn_blocking(std::invocable<> auto &&fn)
        requires(!async_function<decltype(fn)>)
    {
        return spawn_blocking(std::forward<decltype(fn)>(fn), std::monostate{});
    }

private:
    void awake_next() noexcept;

    template<typename TaskLocalStorage>
    auto spawn_impl(async_function<> auto fn) -> join_handle<
        typename std::invoke_result_t<decltype(fn)>::output_type,
        util::types::void_if_monostate<TaskLocalStorage>> {
        using output_type = std::invoke_result_t<decltype(fn)>::output_type;
        auto coro = co_invoke(fn);
        if constexpr (std::is_void_v<output_type>) {
            co_await coro;
            co_return;
        } else {
            co_return co_await coro;
        }
    }

    template<typename TaskLocalStorage>
    auto spawn_blocking_impl(std::invocable<> auto fn)
        -> join_handle<std::invoke_result_t<decltype(fn)>, util::types::void_if_monostate<TaskLocalStorage>> {
        using output_type = std::invoke_result_t<decltype(fn)>;
        if constexpr (std::is_void_v<output_type>) {
            std::invoke(fn);
            co_return;
        } else {
            co_return std::invoke(fn);
        }
    }

    detail::idle_workers_receiver m_idle_workers_rx;

    detail::coroutine_sender m_coroutine_tx;
    std::shared_ptr<std::counting_semaphore<detail::coroutine_queue_capacity + 1>> m_backsem_sync;

    std::unique_ptr<time::timer> m_timer;

    std::vector<std::unique_ptr<worker>> m_workers;
    std::vector<runtime **> m_workers_local_runtime_ptr;

    inline static concurrency::hash_map<std::coroutine_handle<>, worker *> m_corohandle_worker_map;

    inline thread_local static runtime *_current_runtime{nullptr};
};

};  // namespace core

template<typename TaskLocalStorage>
auto spawn(async_function<> auto &&fn, TaskLocalStorage &&task_local_storage) {
    return core::runtime::current().spawn(
        std::forward<decltype(fn)>(fn), std::forward<decltype(task_local_storage)>(task_local_storage));
}

auto spawn(async_function<> auto &&fn) { return spawn(std::forward<decltype(fn)>(fn), std::monostate{}); }

template<typename TaskLocalStorage>
auto spawn_blocking(std::invocable<> auto &&fn, TaskLocalStorage &&task_local_storage)
    requires(!async_function<decltype(fn)>)
{
    return core::runtime::current().spawn_blocking(
        std::forward<decltype(fn)>(fn), std::forward<decltype(task_local_storage)>(task_local_storage));
}

auto spawn_blocking(std::invocable<> auto &&fn)
    requires(!async_function<decltype(fn)>)
{
    return spawn_blocking(std::forward<decltype(fn)>(fn), std::monostate{});
}

};  // namespace asco
