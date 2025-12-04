// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <asco/concurrency/continuous_queue.h>
#include <asco/future.h>
#include <asco/invoke.h>
#include <asco/panic/panic.h>
#include <asco/sync/semaphore.h>
#include <asco/utils/concepts.h>
#include <asco/yield.h>

namespace asco::base {

using namespace concepts;

namespace cq = continuous_queue;

template<move_secure T, bool Core>
    requires(!std::is_void_v<T> && (bool)"Generator type T cannot be void.")
struct generator_base : public future_base<T, true, Core, false, unlimited_semaphore> {
    using base_future = future_base<T, true, Core, false, unlimited_semaphore>;

    struct promise_type;
    using coroutine_handle_type = base_future::coroutine_handle_type;
    using deliver_type = typename base_future::deliver_type;

    struct promise_type : public base_future::promise_type {
        using promise_base = typename base_future::promise_type;

        cq::sender<deliver_type> yield_tx;

        generator_base get_return_object(
            panic::coroutine_trace_handle caller_cthdl =
                *cpptrace::stacktrace::current(1, 1).begin()) noexcept {
            cq::receiver<deliver_type> rx;
            std::tie(yield_tx, rx) = cq::create<deliver_type>();
            auto res = generator_base{promise_base::get_return_object(caller_cthdl), std::move(rx)};
            res.this_task->extra_slot.emplace(false, 0);
            return std::move(res);
        }

        auto yield_value(passing<deliver_type> value) {
            if (yield_tx.push(std::move(value))) [[unlikely]] {
                panic::panic("[ASCO] generator::promise_type::yield_value(): yield queue is closed.");
            }
            promise_base::this_task->extra_slot.get().release();

            return std::suspend_never{};
        }

        void unhandled_exception() noexcept {
            promise_base::unhandled_exception();
            yield_tx.stop();
            promise_base::this_task->extra_slot.get().release();
        }

        void return_void() noexcept {
            yield_tx.stop();
            promise_base::this_task->extra_slot.get().release();
            promise_base::return_void();
        }
    };

    operator bool() const {
        if (base_future::none)
            return false;

        return !yield_rx.is_stopped();
    }

    auto operator()() -> future<std::optional<deliver_type>> {
        auto &sem = base_future::this_task->extra_slot.get();
        co_await sem.acquire();

        if (base_future::this_task->e_thrown.load(morder::acquire) && sem.get_counter() == 0) {
            if (auto &e = base_future::this_task->e_ptr) {
                auto tmp = e;
                e = nullptr;
                base_future::this_task->e_rethrown.store(true, morder::release);
                std::rethrow_exception(tmp);
            }
        }

        for (size_t i{0}; true; ++i) {
            if (auto res = yield_rx.pop())
                co_return std::move(*res);
            else if (res.error() == queue::pop_fail::closed)
                co_return std::nullopt;

            if (i > 100)
                co_await yield<>{};
        }
    }

    bool await_ready() = delete;

    auto await_suspend(std::coroutine_handle<>) = delete;

    deliver_type await_resume() = delete;

    deliver_type await() = delete;

    generator_base() = default;

    generator_base(const generator_base &) = delete;

    generator_base(generator_base &&rhs)
            : base_future(std::move(rhs))
            , yield_rx(std::move(rhs.yield_rx)) {}

    generator_base(base_future &&base_fut, cq::receiver<deliver_type> &&rx)
            : base_future(std::move(base_fut))
            , yield_rx(std::move(rx)) {}

    generator_base &operator=(generator_base &&rhs) {
        if (this == &rhs)
            return *this;

        this->~generator_base();
        new (this) generator_base(std::move(rhs));
        return *this;
    }

private:
    cq::receiver<deliver_type> yield_rx;
};

};  // namespace asco::base

namespace asco {

template<concepts::move_secure T>
using generator = base::generator_base<T, false>;
template<concepts::move_secure T>
using generator_core = base::generator_base<T, true>;

namespace concepts {

template<typename Fn>
struct is_specialization_of_generator_type {
private:
    template<move_secure T, bool Core>
    static std::true_type test(base::generator_base<T, Core> *);

    template<typename>
    static std::false_type test(...);

public:
    static constexpr bool value = decltype(test(std::declval<Fn *>()))::value;
};

template<typename T>
constexpr bool is_specialization_of_generator_type_v = is_specialization_of_generator_type<T>::value;

template<typename G>
concept generator_type = is_specialization_of_generator_type_v<std::remove_cvref_t<G>>;

template<typename Fn, typename... Args>
concept generator_function =
    std::invocable<Fn, Args...> && generator_type<std::invoke_result_t<std::remove_cvref_t<Fn>, Args...>>;

};  // namespace concepts

namespace base {

// Pipe operator
template<generator_type G, async_function<std::remove_cvref_t<G>> Fn>
constexpr auto operator|(G &&source, Fn &&consumer) {
    return co_invoke(consumer, std::forward<std::remove_cvref_t<G>>(source));
}

};  // namespace base

};  // namespace asco
