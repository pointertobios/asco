// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <concepts>
#include <deque>
#include <expected>
#include <limits>
#include <type_traits>
#include <utility>

#include <asco/core/worker.h>
#include <asco/future.h>
#include <asco/join_handle.h>
#include <asco/sync/spinlock.h>
#include <asco/yield.h>

namespace asco::sync {

enum class notify_failed {
    predicate_false,
    no_waiters,
};

class condition_variable final {
public:
    condition_variable() = default;

    condition_variable(const condition_variable &) = delete;
    condition_variable &operator=(const condition_variable &) = delete;

    condition_variable(condition_variable &&) = delete;
    condition_variable &operator=(condition_variable &&) = delete;

    template<std::invocable<> Fn>
        requires(!async_function<Fn> && !spawned_function<Fn> && std::same_as<std::invoke_result_t<Fn>, bool>)
    future<void> wait(Fn predictor) {
        while (true) {
            if (auto g = m_wait_queue.lock()) {
                if (predictor()) {
                    co_return;
                }
                core::awake_token token{};
                g->push_back(token);
                token.suspend();
            }
            co_await this_task::yield();
        }
        co_return;
    }

    template<std::invocable<> Fn>
        requires(!async_function<Fn> && !spawned_function<Fn> && std::same_as<std::invoke_result_t<Fn>, bool>)
    future<bool> wait_once(Fn predicator) {
        if (auto g = m_wait_queue.lock()) {
            if (predicator()) {
                co_return false;
            }
            core::awake_token token{};
            g->push_back(token);
            token.suspend();
        }
        co_await this_task::yield();
        co_return true;
    }

    bool notify_one() {
        auto g = m_wait_queue.lock();
        if (g->empty()) {
            return false;
        }

        auto &token = g->front();
        token.awake();
        g->pop_front();

        return true;
    }

    template<std::invocable<> Fn>
        requires(!async_function<Fn> && !spawned_function<Fn> && std::same_as<std::invoke_result_t<Fn>, bool>)
    std::expected<void, notify_failed> notify_one(Fn &&predicator) {
        auto g = m_wait_queue.lock();
        if (g->empty()) {
            return std::unexpected{notify_failed::no_waiters};
        }
        if (!predicator()) {
            return std::unexpected{notify_failed::predicate_false};
        }

        auto &token = g->front();
        token.awake();
        g->pop_front();

        return {};
    }

    std::size_t notify(std::size_t n) {
        auto g = m_wait_queue.lock();
        std::size_t notified = 0;
        while (!g->empty() && notified < n) {
            auto &token = g->front();
            token.awake();
            g->pop_front();
            ++notified;
        }
        return notified;
    }

    std::size_t notify() { return notify(std::numeric_limits<std::size_t>::max()); }

    template<std::invocable<> Fn>
        requires(!async_function<Fn> && !spawned_function<Fn> && std::same_as<std::invoke_result_t<Fn>, bool>)
    std::expected<std::size_t, notify_failed> notify(Fn &&predicator, std::size_t n) {
        auto g = m_wait_queue.lock();
        if (g->empty()) {
            return std::unexpected{notify_failed::no_waiters};
        }
        if (!predicator()) {
            return std::unexpected{notify_failed::predicate_false};
        }

        std::size_t notified = 0;
        while (!g->empty() && notified < n) {
            auto &token = g->front();
            token.awake();
            g->pop_front();
            ++notified;
        }
        return notified;
    }

    template<std::invocable<> Fn>
        requires(!async_function<Fn> && !spawned_function<Fn> && std::same_as<std::invoke_result_t<Fn>, bool>)
    std::expected<std::size_t, notify_failed> notify(Fn &&predicator) {
        return notify(std::forward<Fn>(predicator), std::numeric_limits<std::size_t>::max());
    }

private:
    spinlock<std::deque<core::awake_token>> m_wait_queue;
};

};  // namespace asco::sync
