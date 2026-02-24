// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <coroutine>
#include <exception>
#include <memory>
#include <semaphore>

#include <asco/core/cancellation.h>
#include <asco/core/wait_for_valid.h>
#include <asco/core/worker.h>
#include <asco/future.h>
#include <asco/util/erased.h>
#include <asco/util/raw_storage.h>
#include <asco/util/safe_erased.h>
#include <asco/util/types.h>

namespace asco {

template<util::types::move_secure Output, typename TaskLocalStorage = void>
class [[nodiscard]] join_handle final {
    friend class core::runtime;

public:
    using output_type = Output;

    class promise_type;

    using coroutine_handle = std::coroutine_handle<promise_type>;

private:
    enum class complete_state {
        not_completed,
        awaitable_waiting,
        completed,
    };

    struct task_state {
        coroutine_handle this_handle;

        std::exception_ptr e_ptr{};
        [[no_unique_address]] util::raw_storage<output_type> value{};
        std::atomic<complete_state> cstate{complete_state::not_completed};
        std::binary_semaphore sync_awaiter{0};

        std::atomic<std::coroutine_handle<>> caller_handle{};

        util::erased bound_lambda{};

        core::cancel_source cancel_source{};

        [[no_unique_address]] util::raw_storage<TaskLocalStorage> task_local_storage{};

        ~task_state() {
            if (this->cstate.load(std::memory_order_acquire) == complete_state::completed) {
                if (!this->e_ptr) {
                    if constexpr (!std::is_void_v<output_type>) {
                        this->value.get()->~output_type();
                    }
                }
            }
            if constexpr (!std::is_void_v<TaskLocalStorage>) {
                this->task_local_storage.get()->~TaskLocalStorage();
            }
        }

        bool try_mark_completed() noexcept {
            complete_state e;
            do {
                e = this->cstate.load(std::memory_order::acquire);
                if (e == complete_state::completed) {
                    return false;
                }
            } while (!this->cstate.compare_exchange_weak(
                e, complete_state::completed, std::memory_order::acq_rel, std::memory_order::relaxed));
            return true;
        }
    };

public:
    static constexpr bool output_void = std::is_void_v<output_type>;

    class promise_base {
    public:
        std::shared_ptr<task_state> m_state;
    };

    class promise_void_mixin : public promise_base {
    public:
        void return_void() noexcept {
            if (this->m_state->try_mark_completed()) {
                this->m_state->sync_awaiter.release();
                auto handle = this->m_state->caller_handle.load(std::memory_order::acquire);
                if (handle) {
                    core::worker::of_handle(handle).awake_handle(handle);
                }
            }
        }
    };

    class promise_nonvoid_mixin : public promise_base {
    public:
        void return_value(output_type value) noexcept {
            if (this->m_state->try_mark_completed()) {
                new (this->m_state->value.get())
                    util::types::monostate_if_void<output_type>{std::move(value)};
                this->m_state->sync_awaiter.release();
                auto handle = this->m_state->caller_handle.load(std::memory_order::acquire);
                if (handle) {
                    core::worker::of_handle(handle).awake_handle(handle);
                }
            }
        }
    };

    using promise_spanwidth = std::conditional_t<output_void, promise_void_mixin, promise_nonvoid_mixin>;

    class promise_type final : public promise_spanwidth {
    public:
        join_handle get_return_object() noexcept {
            this->m_state = std::make_shared<task_state>(coroutine_handle::from_promise(*this));
            return join_handle{this->m_state};
        }

        auto initial_suspend() noexcept { return std::suspend_always{}; }

        void unhandled_exception() noexcept {
            if (this->m_state->try_mark_completed()) {
                this->m_state->e_ptr = std::current_exception();
                this->m_state->sync_awaiter.release();
                auto handle = this->m_state->caller_handle.load(std::memory_order::acquire);
                if (handle) {
                    core::worker::of_handle(handle).awake_handle(handle);
                }
            }
        }

        auto final_suspend() noexcept {
            if (this->m_state->try_mark_completed()) {
                this->m_state->sync_awaiter.release();
                auto handle = this->m_state->caller_handle.load(std::memory_order::acquire);
                if (handle) {
                    core::worker::of_handle(handle).awake_handle(handle);
                }
            }

            struct final_awaitable {
                coroutine_handle this_handle;

                bool await_ready() noexcept { return false; }

                void await_suspend(std::coroutine_handle<>) noexcept {
                    auto h = core::worker::current().pop_handle();
                    asco_assert(this_handle == h);
                    this_handle.destroy();
                    return;
                }

                void await_resume() noexcept {}
            };
            return final_awaitable{this->m_state->this_handle};
        }
    };

    bool await_ready() noexcept {
        return this->m_state->cstate.load(std::memory_order::acquire) == complete_state::completed;
    }

    void await_suspend(std::coroutine_handle<> handle) noexcept {
        this->m_state->caller_handle.store(handle, std::memory_order::release);
        complete_state e;
        do {
            e = this->m_state->cstate.load(std::memory_order::acquire);
            if (e == complete_state::completed) {
                return;
            }
        } while (!this->m_state->cstate.compare_exchange_weak(
            e, complete_state::awaitable_waiting, std::memory_order::acq_rel, std::memory_order::relaxed));

        core::worker::current().suspend_current_handle(handle);
    }

    output_type await_resume() {
        this->m_state->cstate.load(std::memory_order::acquire);

        if (this->m_state->e_ptr) {
            std::rethrow_exception(this->m_state->e_ptr);
        }
        if constexpr (output_void) {
            return;
        } else {
            return std::move(*this->m_state->value.get());
        }
    }

    void detach(this join_handle &&) {}

    util::erased &bind_lambda(util::erased &&l) noexcept {
        this->m_state->bound_lambda = std::move(l);
        return this->m_state->bound_lambda;
    }

    void cancel() noexcept {
        if (this->m_state->try_mark_completed()) {
            this->m_state->e_ptr = std::make_exception_ptr(core::coroutine_cancelled{});
            this->m_state->cancel_source.request_cancel();

            this->m_state->sync_awaiter.release();
            auto handle = this->m_state->caller_handle.load(std::memory_order::acquire);
            if (handle) {
                core::worker::of_handle(handle).awake_handle(handle);
            }

            handle = this->m_state->this_handle;
            if (auto w = core::worker::optional_of_handle(handle)) {
                if (auto th = w->top_of_join_handle(handle)) {
                    w->awake_handle(th);
                }
            }
        }
    }

    join_handle(const join_handle &) = delete;
    join_handle &operator=(const join_handle &) = delete;

    join_handle(join_handle &&rhs) noexcept = default;
    join_handle &operator=(join_handle &&rhs) noexcept = default;

private:
    join_handle(std::shared_ptr<task_state> state)
            : m_state{std::move(state)} {}

    core::cancel_source &get_cancel_source() noexcept { return this->m_state->cancel_source; }

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

    util::safe_erased initialize_task_local_storage(util::types::monostate_if_void<TaskLocalStorage> &&tls) {
        if constexpr (!std::is_void_v<TaskLocalStorage>) {
            new (m_state->task_local_storage.get()) TaskLocalStorage{std::forward<TaskLocalStorage>(tls)};
            return util::safe_erased{util::safe_erased::ref{*m_state->task_local_storage.get()}};
        } else {
            return util::safe_erased{};
        }
    }

    std::shared_ptr<task_state> m_state;
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
