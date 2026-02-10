// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <coroutine>
#include <memory>
#include <semaphore>

#include <asco/core/worker.h>
#include <asco/future.h>
#include <asco/util/erased.h>
#include <asco/util/raw_storage.h>
#include <asco/util/types.h>

namespace asco {

template<util::types::move_secure Output>
class join_handle final {
    friend class core::runtime;

public:
    using output_type = Output;

    class promise_type;

    using coroutine_handle = std::coroutine_handle<promise_type>;

private:
    struct join_state {
        coroutine_handle this_handle;

        std::exception_ptr e_ptr;
        util::raw_storage<util::types::monostate_if_void<output_type>> value;
        std::atomic_bool result_completed{false};
        std::binary_semaphore sync_awaiter{0};

        std::coroutine_handle<> caller_handle;

        std::unique_ptr<util::erased> bound_lambda;
    };

public:
    static constexpr bool output_void = std::is_void_v<output_type>;

    class promise_base {
    public:
        std::shared_ptr<join_state> m_state;
    };

    class promise_void_mixin : public promise_base {
    public:
        void return_void() noexcept {}
    };

    class promise_nonvoid_mixin : public promise_base {
    public:
        void return_value(output_type value) noexcept {
            new (this->m_state->value.get()) util::types::monostate_if_void<output_type>{std::move(value)};
            this->m_state->result_completed.store(true, std::memory_order::release);
        }
    };

    using promise_spanwidth = std::conditional_t<output_void, promise_void_mixin, promise_nonvoid_mixin>;

    class promise_type final : public promise_spanwidth {
    public:
        join_handle get_return_object() noexcept {
            this->m_state = std::make_shared<join_state>(coroutine_handle::from_promise(*this));
            return join_handle{this->m_state};
        }

        auto initial_suspend() noexcept { return std::suspend_always{}; }

        void unhandled_exception() noexcept {
            this->m_state->e_ptr = std::current_exception();
            this->m_state->result_completed.store(true, std::memory_order::release);
        }

        auto final_suspend() noexcept {
            struct final_awaitable {
                coroutine_handle this_handle;

                bool await_ready() noexcept { return false; }

                void await_suspend(std::coroutine_handle<>) noexcept {
                    this_handle.destroy();
                    return;
                }

                void await_resume() noexcept {}
            };
            this->m_state->sync_awaiter.release();
            auto handle = this->m_state->caller_handle;
            if (handle) {
                core::worker::of_handle(handle).awake_handle(handle);
            }
            return final_awaitable{this->m_state->this_handle};
        }
    };

    bool await_ready() noexcept { return this->m_state->result_completed.load(std::memory_order::acquire); }

    void await_suspend(std::coroutine_handle<> handle) noexcept {
        this->m_state->caller_handle = handle;

        core::worker::current().suspend_handle(handle);
    }

    output_type await_resume() {
        this->m_state->result_completed.load(std::memory_order::acquire);

        if (this->m_state->e_ptr) {
            std::rethrow_exception(this->m_state->e_ptr);
        }
        if constexpr (output_void) {
            return;
        } else {
            return std::move(*this->m_state->value);
        }
    }

    void bind_lambda(std::unique_ptr<util::erased> l) noexcept { this->m_state->bound_lambda = std::move(l); }

    join_handle(const join_handle &) = delete;
    join_handle &operator=(const join_handle &) = delete;

    join_handle(join_handle &&rhs) noexcept = default;
    join_handle &operator=(join_handle &&rhs) noexcept = default;

private:
    join_handle(std::shared_ptr<join_state> state)
            : m_state{std::move(state)} {}

    output_type await() {
        this->m_state->sync_awaiter.acquire();

        if (this->m_state->e_ptr) {
            std::rethrow_exception(this->m_state->e_ptr);
        }
        if constexpr (output_void) {
            return;
        } else {
            return std::move(*this->m_state->value.get());
        }
    }

    std::shared_ptr<join_state> m_state;
};

namespace detail {

template<typename Fn>
struct is_specialization_of_join_handle {
private:
    template<util::types::move_secure T>
    static std::true_type test(join_handle<T> *);

    template<typename T>
    static std::false_type test(T *);

public:
    static constexpr bool value = decltype(test(std::declval<Fn *>()))::value;
};

template<typename T>
constexpr bool is_specialization_of_join_handle_v = is_specialization_of_join_handle<T>::value;
};  // namespace detail

template<typename F>
concept join_handle_type = detail::is_specialization_of_join_handle_v<std::remove_cvref_t<F>>;

template<typename Fn, typename... Args>
concept spawned_function =
    std::invocable<Fn, Args...> && join_handle_type<std::invoke_result_t<std::remove_cvref_t<Fn>, Args...>>;

};  // namespace asco
