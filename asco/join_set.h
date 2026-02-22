// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <vector>

#include <asco/core/runtime.h>
#include <asco/join_handle.h>
#include <asco/sync/channel.h>
#include <asco/util/types.h>

namespace asco {

template<util::types::move_secure Output>
class join_set final {
public:
    using output_type = Output;

    join_set()
            : join_set{core::runtime::current()} {}

    join_set(core::runtime &rt)
            : m_runtime{rt} {
        std::tie(m_tx, m_rx) = sync::channel<output_type>();
    }

    join_set(const join_set &) = delete;
    join_set &operator=(const join_set &) = delete;

    join_set(join_set &&) = delete;
    join_set &operator=(join_set &&) = delete;

    void spawn(async_function<> auto &&fn) {
        m_runtime
            .spawn([tx = m_tx, fn = std::forward<decltype(fn)>(fn)]() mutable -> future<void> {
                if constexpr (std::is_void_v<output_type>) {
                    co_await co_invoke(std::forward<decltype(fn)>(fn));
                    co_await tx.send();
                } else {
                    co_await tx.send(co_await co_invoke(std::forward<decltype(fn)>(fn)));
                }
            })
            .detach();
        m_task_count.fetch_add(1, std::memory_order::acq_rel);
    }

    void spawn_blocking(std::invocable<> auto &&fn)
        requires(!async_function<decltype(fn)>)
    {
        spawn([fn = std::forward<decltype(fn)>(fn)]() -> future<output_type> {
            if constexpr (std::is_void_v<output_type>) {
                std::invoke(std::forward<decltype(fn)>(fn));
                co_return;
            } else {
                co_return std::invoke(std::forward<decltype(fn)>(fn));
            }
        });
    }

    future<std::conditional_t<std::is_void_v<output_type>, bool, std::optional<output_type>>>
    operator co_await() {
        if (m_task_count.load(std::memory_order::acquire) == 0) {
            co_return std::nullopt;
        }
        auto res = co_await m_rx.recv();
        if (res) {
            m_task_count.fetch_sub(1, std::memory_order::acq_rel);
        }
        co_return res;
    }

    future<std::vector<util::types::monostate_if_void<output_type>>> join_all()
        requires(!std::is_void_v<output_type>)
    {
        m_rx.stop();

        std::vector<util::types::monostate_if_void<output_type>> res;
        std::optional<output_type> buf;
        while ((buf = co_await m_rx.recv())) {
            res.emplace_back(std::move(*buf));
        }
        co_return res;
    }

    future<std::size_t> join_all()
        requires(std::is_void_v<output_type>)
    {
        m_rx.stop();

        std::size_t res{0};
        while (co_await m_rx.recv()) {
            res++;
        }
        co_return res;
    }

private:
    core::runtime &m_runtime;
    sync::sender<output_type> m_tx{};
    sync::receiver<output_type> m_rx{};
    std::atomic_size_t m_task_count{0};
};

};  // namespace asco
