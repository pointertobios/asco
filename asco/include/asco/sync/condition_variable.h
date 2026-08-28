// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "asco/container/hash_map.h"
#include "asco/core/worker.h"
#include "asco/future.h"
#include "asco/sync/rwspinlock.h"
#include "asco/sync/spinlock.h"

namespace asco::sync {

class condition_variable {
    using awake_token = core::awake_token;

    template<typename Fn = void>
    class wait_awaitable {
        friend class condition_variable;

    public:
        void set_predicator(Fn *pred) noexcept { m_pred = pred; }

        bool await_ready() {
            if constexpr (!std::is_void_v<Fn>) {
                m_pred_result = (*m_pred)();
                return m_pred_result;
            } else {
                return false;
            }
        }

        bool await_suspend(std::coroutine_handle<> handle) {
            auto g = m_cv.m_suspend_lock.lock();
            awake_token tk{};
            if constexpr (!std::is_void_v<Fn>) {
                m_pred_result = (*m_pred)();
                if (m_pred_result) {
                    return false;
                }
            }
            tk.suspend(handle);
            auto non_tk = awake_token::none();
            if (!m_cv.m_fast_awake_token.compare_exchange_strong(
                    non_tk, tk, morder::acq_rel, morder::relaxed)) {
                auto guard = condition_variable::s_parking_lot.read();
                guard->get(&m_cv)->push_back(tk);
            }
            return true;
        }

        std::conditional_t<std::is_void_v<Fn>, void, bool> await_resume() const noexcept {
            if constexpr (!std::is_void_v<Fn>) {
                return m_pred_result;
            }
        }

    private:
        wait_awaitable(condition_variable &cv) noexcept
                : m_cv{cv} {}

        condition_variable &m_cv;
        Fn *m_pred{nullptr};
        bool m_pred_result{false};
    };

public:
    condition_variable() noexcept;
    ~condition_variable() noexcept;

    condition_variable(const condition_variable &) = delete;
    condition_variable &operator=(const condition_variable &) = delete;

    condition_variable(condition_variable &&) = delete;
    condition_variable &operator=(condition_variable &&) = delete;

    wait_awaitable<> operator()() noexcept { return wait_awaitable{*this}; }

    future<> operator()(concepts::verified_invocable<bool> auto pred) {
        while (true) {
            auto awaitable = wait_awaitable<decltype(pred)>{*this};
            awaitable.set_predicator(&pred);
            if (co_await awaitable) {
                break;
            }
        }
    }

    bool notify_one() noexcept;
    usize notify_all() noexcept;

private:
    std::atomic<awake_token> m_fast_awake_token{awake_token::none()};
    spinlock<> m_suspend_lock{};

    inline static rwspinlock<container::hash_map<condition_variable *, std::vector<awake_token>>, true>
        s_parking_lot{};
};

};  // namespace asco::sync
