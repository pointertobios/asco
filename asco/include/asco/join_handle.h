// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <coroutine>
#include <exception>
#include <memory>
#include <utility>

#include "asco/assert.h"
#include "asco/concepts.h"
#include "asco/core/future_state.h"
#include "asco/core/worker.h"
#include "asco/macros.h"
#include "asco/types/raw_storage.h"
#include "asco/types/specialization.h"

namespace asco {

template<typename T>
class join_handle final {
public:
    using output_type = T;

    struct promise_type;
    using coroutine_handle = std::coroutine_handle<promise_type>;

    using future_state = core::future_state;
    using task_block_base = core::task_block_base;

private:
    struct task_block : public task_block_base {
        [[ASCO_NO_UNIQUE_ADDRESS]] types::raw_storage<T> m_storage{};
        std::exception_ptr m_exception{};

        task_block(future_state state) noexcept
                : task_block_base{state} {}
    };

    struct promise_base {
        std::shared_ptr<task_block> m_task_block;

        coroutine_handle m_this_coroutine{};

        std::suspend_always initial_suspend() noexcept { return {}; }

        void unhandled_exception() noexcept { m_task_block->m_exception = std::current_exception(); }

        auto final_suspend() noexcept {
            struct final_awaitable {
                coroutine_handle this_coroutine;

                bool await_ready() noexcept { return false; }

                void await_suspend(std::coroutine_handle<>) noexcept { this_coroutine.destroy(); }

                void await_resume() noexcept {}
            };
            {
                auto s = m_task_block->m_state.exchange(future_state::complete, morder::acq_rel);
                if (s != future_state::complete) {
                    if (s == future_state::awaited) {
                        promise_base::m_task_block->m_awake_token.awake();
                    }
                    promise_base::m_task_block->m_sync_awaiter.release();
                }
            }
            using core::worker;
            if (worker::exists()) {
                auto &w = worker::current();
                w.set_next_resume(std::coroutine_handle{});
                w.set_suspend_now();
            }
            return final_awaitable{m_this_coroutine};
        }

    protected:
        bool return_state_valid() const noexcept {
            auto s = m_task_block->m_state.load(morder::relaxed);
            return s == future_state::non_complete || s == future_state::awaited;
        }
    };

    struct promise_void_mixin : public promise_base {
        void return_void() noexcept {
            ASCO_ASSERT(promise_base::return_state_valid());

            auto s = promise_base::m_task_block->m_state.exchange(future_state::complete, morder::acq_rel);
            if (s == future_state::awaited) {
                promise_base::m_task_block->m_awake_token.awake();
            }
            promise_base::m_task_block->m_sync_awaiter.release();
        }
    };

    struct promise_value_mixin : public promise_base {
        void return_value(try_move_t<T> value) noexcept {
            ASCO_ASSERT(promise_base::return_state_valid());

            promise_base::m_task_block->m_storage.emplace(try_move(value));
            auto s = promise_base::m_task_block->m_state.exchange(future_state::complete, morder::acq_rel);
            if (s == future_state::awaited) {
                promise_base::m_task_block->m_awake_token.awake();
            }
            promise_base::m_task_block->m_sync_awaiter.release();
        }
    };

    using promise_base_type =
        std::conditional_t<concepts::is_void<T>, promise_void_mixin, promise_value_mixin>;

public:
    struct promise_type : public promise_base_type {
        join_handle get_return_object() {
            auto this_coroutine = coroutine_handle::from_promise(*this);
            promise_base::m_this_coroutine = this_coroutine;

            auto task_block_ptr = std::make_shared<task_block>(future_state::non_complete);
            promise_base::m_task_block = task_block_ptr;

            return join_handle{this_coroutine, std::move(task_block_ptr)};
        }
    };

    join_handle() = default;

    join_handle(const join_handle &) = delete;
    join_handle &operator=(const join_handle &) = delete;

    join_handle(join_handle &&rhs) noexcept
            : m_this_coroutine{std::move(rhs.m_this_coroutine)}
            , m_task_block{std::move(rhs.m_task_block)} {
        ASCO_ASSERT(!is_empty());

        rhs.m_this_coroutine = coroutine_handle{};
    }

    join_handle &operator=(join_handle &&rhs) noexcept {
        ASCO_ASSERT(!rhs.is_empty());

        if (this != &rhs) {
            this->~join_handle();
            new (this) join_handle{std::move(rhs)};
        }
        return *this;
    }

    bool is_empty() const noexcept { return !m_task_block; }

    bool await_ready() noexcept {
        ASCO_ASSERT(!is_empty());

        return m_task_block->m_state.load(morder::acquire) == future_state::complete;
    }

    bool await_suspend(std::coroutine_handle<> waiter_coroutine) noexcept {
        ASCO_ASSERT(!is_empty());

        core::awake_token awake_token{};
        m_task_block->m_awake_token = awake_token;
        if (future_state e{future_state::non_complete}; m_task_block->m_state.compare_exchange_strong(
                e, future_state::awaited, morder::acq_rel, morder::relaxed)) {
            awake_token.suspend(waiter_coroutine);
            return true;
        } else {
            ASCO_ASSERT(e != future_state::awaited, "禁止重复 co_await");
            return false;
        }
    }

    T await_resume() {
        ASCO_ASSERT(!is_empty());

        ASCO_ASSERT(m_task_block->m_state.load(morder::acquire) == future_state::complete);

        if (auto e = m_task_block->m_exception) {
            std::rethrow_exception(e);
        }

        if constexpr (concepts::is_void<T>) {
            m_task_block.reset();
            return;
        } else {
            T res{try_move(*m_task_block->m_storage.get())};
            m_task_block->m_storage.destroy();
            m_task_block.reset();
            return res;
        }
    }

    T blocking_await() {
        m_task_block->m_sync_awaiter.acquire();
        return await_resume();
    }

    void bind_invocable(types::erased &&invocable) noexcept {
        m_task_block->m_bound_invocable = std::move(invocable);
    }

    core::task_item get_task_item() const noexcept { return {m_this_coroutine, &*m_task_block}; }

private:
    join_handle(coroutine_handle this_coroutine, std::shared_ptr<task_block> task_block_ptr)
            : m_this_coroutine{this_coroutine}
            , m_task_block{task_block_ptr} {}

    coroutine_handle m_this_coroutine;
    std::shared_ptr<task_block> m_task_block;
};

template<typename F>
concept join_handle_type = types::specialization_of<std::remove_cvref_t<F>, join_handle>;

};  // namespace asco
