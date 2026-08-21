// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <tuple>

#include "asco/co_invoke.h"
#include "asco/concepts.h"
#include "asco/sync/mpsc.h"
#include "asco/this_task.h"
#include "asco/types/fuck_void.h"

namespace asco::task {

template<concepts::non_void Output>
class join_set final {
    using mpsc_element_type = types::fuck_void<Output>;

public:
    using output_type = Output;

    join_set() noexcept
            : join_set{this_task::host_runtime()} {}

    join_set(core::runtime &rt) noexcept
            : m_runtime{rt} {
        std::tie(m_rx, m_rx) = sync::mpsc<output_type>::channel();
    }

    join_set(const join_set &) = delete;
    join_set &operator=(const join_set &) = delete;

    join_set(join_set &&) = delete;
    join_set &operator=(join_set &&) = delete;

    void spawn(async_function<> auto &&fn) {
        m_runtime.spawn([tx = m_tx, fn = std::forward<decltype(fn)>(fn)] mutable -> future<> {
            if constexpr (concepts::is_void<output_type>) {
                co_await co_invoke(std::forward<decltype(fn)>(fn));
                co_await tx.send(std::monostate{});
            } else {
                co_await tx.send(co_await co_invoke(std::forward<decltype(fn)>(fn)));
            }
        });
        m_task_count.fetch_add(1, morder::acq_rel);
    }

    void spawn_blocking(std::invocable<> auto &&fn)
        requires(!async_function<decltype(fn)>)
    {
        m_runtime.spawn_blocking(std::forward<decltype(fn)>(fn));
    }

    future<std::conditional_t<concepts::non_void<output_type>, std::optional<output_type>, bool>>
    operator co_await() {
        if (m_task_count.load(morder::acquire) == 0) {
            if constexpr (concepts::is_void<output_type>) {
                co_return false;
            } else {
                co_return std::nullopt;
            }
        }
        auto res = co_await m_rx.recv();
        m_task_count.fetch_sub(1, morder::acq_rel);
        co_return res;
    }

    future<std::vector<types::fuck_void<output_type>>> join_all()
        requires(concepts::non_void<output_type>)
    {
        m_rx.stop();

        std::vector<types::fuck_void<output_type>> res;
        while (m_task_count.fetch_sub(1, morder::acq_rel)) {
            res.emplace_back(co_await m_rx.recv());
        }
        co_return res;
    }

    future<usize> join_all()
        requires(concepts::is_void<output_type>)
    {
        m_rx.stop();

        usize res = m_task_count.load(morder::acquire);
        while (m_task_count.fetch_sub(1, morder::acq_rel)) {
            co_await m_rx.recv();
        }
        return res;
    }

private:
    core::runtime &m_runtime;
    sync::mpsc<mpsc_element_type>::sender m_tx{};
    sync::mpsc<mpsc_element_type>::receiver m_rx{};
    std::atomic<usize> m_task_count{0};
};

};  // namespace asco::task
