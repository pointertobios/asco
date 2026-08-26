// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <coroutine>
#include <exception>
#include <utility>

#include "asco/assert.h"
#include "asco/concepts.h"
#include "asco/core/future_state.h"
#include "asco/macros.h"
#include "asco/types/erased.h"
#include "asco/types/raw_storage.h"
#include "asco/types/specialization.h"
#include "asco/types/try_move.h"

namespace asco {

#ifdef ASCO_DEBUG_ENABLED
namespace detail {

void future_trace_start(std::source_location sl);
void future_trace_end();

};  // namespace detail
#endif

template<typename T = void>
class [[nodiscard("A future<T> must always be co_await'ed once")]] future final {
public:
    using output_type = T;

    struct promise_type;
    using coroutine_handle = std::coroutine_handle<promise_type>;

    using future_state = core::future_state;

private:
    struct promise_base {
        friend class future;

        future *m_future{};
        coroutine_handle m_this_coroutine{};

#ifdef ASCO_DEBUG_ENABLED
        std::source_location m_location;
#endif

        ~promise_base() noexcept { m_this_coroutine.destroy(); }

        std::suspend_always initial_suspend() noexcept { return {}; }

        void unhandled_exception() noexcept { m_future->m_exception = std::current_exception(); }

        auto final_suspend() noexcept {
            struct final_awaitable {
                std::coroutine_handle<> waiter_coroutine;

                bool await_ready() const noexcept { return false; }

                auto await_suspend(std::coroutine_handle<>) const noexcept { return waiter_coroutine; }

                void await_resume() const noexcept {}
            };
            if (m_future->m_state != future_state::complete) {
                m_future->m_state = future_state::complete;
            }
            return final_awaitable{m_future->m_waiter_coroutine};
        }
    };

    struct promise_void_mixin : public promise_base {
        void return_void() {
            ASCO_ASSERT(promise_base::m_future->m_state == future_state::non_complete);

            promise_base::m_future->m_state = future_state::complete;
        }
    };

    struct promise_value_mixin : public promise_base {
        void return_value(try_move_t<T> value) {
            ASCO_ASSERT(promise_base::m_future->m_state == future_state::non_complete);

            promise_base::m_future->m_storage.emplace(try_move(value));
            promise_base::m_future->m_state = future_state::complete;
        }
    };

    using promise_base_type =
        std::conditional_t<concepts::is_void<T>, promise_void_mixin, promise_value_mixin>;

public:
    struct promise_type : public promise_base_type {
        future get_return_object(std::source_location sl = std::source_location::current()) noexcept {
#ifdef ASCO_DEBUG_ENABLED
            promise_base::m_location = sl;
#endif
            promise_base::m_this_coroutine = coroutine_handle::from_promise(*this);
            return future{this, promise_base::m_this_coroutine};
        }
    };

    future() = default;

    future(const future &) = delete;
    future &operator=(const future &) = delete;

    future(future &&rhs) noexcept
            : m_promise{rhs.m_promise}
            , m_state{rhs.m_state}
            , m_this_coroutine{rhs.m_this_coroutine}
            , m_waiter_coroutine{rhs.m_waiter_coroutine}
            , m_bound_invocable{std::move(rhs.m_bound_invocable)} {
        ASCO_ASSERT(!rhs.is_empty());

        m_promise->m_future = this;
    }

    future &operator=(future &&rhs) noexcept {
        ASCO_ASSERT(!is_empty() && !rhs.is_empty());

        if (this != &rhs) {
            m_promise = rhs.m_promise;
            m_state = rhs.m_state;
            m_promise->m_future = this;
        }
        return *this;
    }

    bool is_empty() const noexcept { return m_state == future_state::empty; }

    bool await_ready() noexcept {
        ASCO_ASSERT(!is_empty());

        return false;
    }

    auto await_suspend(std::coroutine_handle<> waiter) noexcept {
        ASCO_ASSERT(!is_empty());

#ifdef ASCO_DEBUG_ENABLED
        detail::future_trace_start(m_promise->m_location);
#endif

        m_waiter_coroutine = waiter;
        return m_this_coroutine;
    }

    T await_resume() {
        ASCO_ASSERT(!is_empty());

#ifdef ASCO_DEBUG_ENABLED
        detail::future_trace_end();
#endif

        if (m_exception) {
            std::rethrow_exception(m_exception);
        }

        if constexpr (concepts::is_void<T>) {
            m_state = future_state::empty;
            return;
        } else {
            T res{try_move(*m_storage.get())};
            m_storage.destroy();
            m_state = future_state::empty;
            return res;
        }
    }

    void bind_invocable(types::erased &&invocable) noexcept { m_bound_invocable = std::move(invocable); }

private:
    future(promise_type *promise, coroutine_handle this_coroutine) noexcept
            : m_promise{promise}
            , m_state{future_state::non_complete}
            , m_this_coroutine{this_coroutine} {
        m_promise->m_future = this;
    }

    promise_type *m_promise{nullptr};

    future_state m_state{future_state::empty};
    [[ASCO_NO_UNIQUE_ADDRESS]] types::raw_storage<T> m_storage{};
    std::exception_ptr m_exception{};

    coroutine_handle m_this_coroutine{};
    std::coroutine_handle<> m_waiter_coroutine{};

    types::erased m_bound_invocable{};
};

template<typename F>
concept future_type = types::specialization_of<std::remove_cvref_t<F>, future>;

};  // namespace asco
