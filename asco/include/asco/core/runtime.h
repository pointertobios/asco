// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include <thread>

#include "asco/assert.h"
#include "asco/co_invoke.h"
#include "asco/concurrency/mpsc.h"
#include "asco/core/blocking_worker.h"
#include "asco/core/predecl.h"
#include "asco/core/worker.h"
#include "asco/sync/rwspinlock.h"
#include "asco/util/rng.h"

namespace asco::core {

class runtime;

class runtime_config {
    friend class runtime;

public:
    runtime_config() = default;

    runtime build() &&;
    std::unique_ptr<runtime> build_ptr() &&;

    runtime_config single_threaded() &&;
    runtime_config multi_threaded(usize n = std::thread::hardware_concurrency()) &&;

private:
    bool m_multi_thread{true};
    usize m_concurrency{std::thread::hardware_concurrency()};
};

class runtime final {
    friend class runtime_config;

public:
    explicit runtime();
    explicit runtime(runtime_config config);
    ~runtime();

    usize worker_count() const { return m_workers.size(); }
    worker &get_worker(usize wid) { return *m_workers[wid]; }

    template<typename... Args>
    auto spawn(async_function<Args...> auto &&fn, Args &&...args) {
        auto jh = task_coroutine(co_invoke(std::forward<decltype(fn)>(fn), std::forward<Args>(args)...));

        usize x;
        if (auto wid = m_acceptible_worker_rx.recv(); wid.has_value()) {
            x = wid.value();
        } else {
            thread_local std::uniform_int_distribution<usize> w{0, m_workers.size() - 1};
            x = w(util::rng());
        }

        auto ta = jh.get_task_item();
        auto &target_worker = *m_workers[x];

        if (m_senders[x].send(try_move(ta))) {
            target_worker.fetch_task();
            (void)m_senders[x].send(try_move(ta));
        }
        target_worker.awake();
        return jh;
    }

    template<typename... Args>
    auto block_on(async_function<Args...> auto &&fn, Args &&...args) {
        ASCO_ASSERT(m_multi_threaded);

        auto jh = spawn(std::forward<decltype(fn)>(fn), std::forward<Args>(args)...);

        if constexpr (concepts::is_void<typename decltype(jh)::output_type>) {
            jh.blocking_await();
        } else {
            return jh.blocking_await();
        }
    }

    template<typename... Args>
    auto spawn_blocking(std::invocable<Args...> auto &&fn, Args &&...args)
        requires(!async_function<decltype(fn), Args...>)
    {
        ASCO_ASSERT(m_multi_threaded);

        auto jh = blocking_task_coroutine(std::forward<decltype(fn)>(fn), std::forward<Args>(args)...);

        usize x;
        if (auto wid = m_acceptible_blocking_worker_rx.recv(); wid.has_value()) {
            x = wid.value();
        } else {
            auto [tx, rx] = concurrency::mpsc<task_item>::queue();
            m_blocking_senders.write()->emplace_back(std::move(tx));
            auto atx = m_acceptible_blocking_worker_tx;

            auto g = m_blocking_workers.write();
            x = m_worker_id_gen++;
            auto &w =
                g->emplace_back(std::make_unique<blocking_worker>(this, x, std::move(rx), std::move(atx)));
            { auto _ = std::move(g); }

            w->start();
        }
        x -= m_blocking_pool_start;

        (void)m_blocking_senders.read()->at(x).send(jh.get_task_item());
        m_blocking_workers.read()->at(x)->awake();
    }

    void main_loop();

    void stop();

private:
    auto task_coroutine(future_type auto future_value)
        -> join_handle<typename decltype(future_value)::output_type> {
        if (!m_multi_threaded) {
            stop();
        }
        using output_type = typename decltype(future_value)::output_type;
        if constexpr (concepts::is_void<output_type>) {
            co_await future_value;
            co_return;
        } else {
            co_return co_await future_value;
        }
    }

    template<typename... Args>
    auto blocking_task_coroutine(std::invocable<Args...> auto fn, Args &&...args)
        -> join_handle<std::invoke_result_t<decltype(fn)>> {
        if constexpr (concepts::is_void<std::invoke_result_t<decltype(fn)>>) {
            std::invoke(fn, args...);
            co_return;
        } else {
            co_return std::invoke(fn, args...);
        }
    }

    const bool m_multi_threaded;
    const usize m_blocking_pool_start;

    std::stop_source m_stop;

    usize m_worker_id_gen;

    std::vector<std::unique_ptr<worker>> m_workers{};
    std::vector<task_sender> m_senders{};
    concurrency::mpsc<usize>::receiver m_acceptible_worker_rx;

    sync::rwspinlock<std::vector<std::unique_ptr<blocking_worker>>> m_blocking_workers;
    sync::rwspinlock<std::vector<task_sender>, true> m_blocking_senders{};
    concurrency::mpsc<usize>::receiver m_acceptible_blocking_worker_rx;
    concurrency::mpsc<usize>::sender m_acceptible_blocking_worker_tx;
};

};  // namespace asco::core
