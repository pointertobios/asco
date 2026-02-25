// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <concepts>
#include <coroutine>
#include <exception>
#include <memory_resource>
#include <type_traits>

#include <asco/core/worker.h>
#include <asco/panic.h>
#include <asco/util/erased.h>
#include <asco/util/raw_storage.h>
#include <asco/util/types.h>

namespace asco {

namespace allocator {

inline std::pmr::polymorphic_allocator<> &get() noexcept {
    static util::raw_storage<std::pmr::synchronized_pool_resource> pool_storage;
    static std::once_flag flag;
    std::call_once(flag, [] { new (pool_storage.get()) std::pmr::synchronized_pool_resource{}; });
    static std::pmr::polymorphic_allocator<> alloc{pool_storage.get()};
    return alloc;
}

};  // namespace allocator

template<util::types::move_secure Output>
class [[nodiscard]] future final {
    friend class core::runtime;

public:
    using output_type = Output;

    class promise_type;

    using coroutine_handle = std::coroutine_handle<promise_type>;

    static constexpr bool output_void = std::is_void_v<output_type>;

    class promise_base {
    public:
        future *m_future_object;
    };

    class promise_void_mixin : public promise_base {
    public:
        void return_void() noexcept {
            this->m_future_object->m_result_completed.store(true, std::memory_order::release);
        }
    };

    class promise_nonvoid_mixin : public promise_base {
    public:
        void return_value(output_type value) noexcept {
            new (this->m_future_object->m_value.get())
                util::types::monostate_if_void<output_type>{std::move(value)};
            this->m_future_object->m_result_completed.store(true, std::memory_order::release);
        }
    };

    using promise_spanwidth = std::conditional_t<output_void, promise_void_mixin, promise_nonvoid_mixin>;

    class promise_type final : public promise_spanwidth {
    public:
        void *operator new(std::size_t size) noexcept { return allocator::get().allocate(size); }

        void operator delete(void *ptr, std::size_t size) noexcept {
            allocator::get().deallocate(reinterpret_cast<std::byte *>(ptr), size);
        }

        static future get_return_object_on_allocation_failure() { throw std::bad_alloc(); }

        future get_return_object() noexcept {
            return future{std::coroutine_handle<promise_type>::from_promise(*this), &this->m_future_object};
        }

        auto initial_suspend() noexcept { return std::suspend_always{}; }

        void unhandled_exception() noexcept {
            this->m_future_object->m_e_ptr = std::current_exception();
            this->m_future_object->m_result_completed.store(true, std::memory_order::release);
        }

        auto final_suspend() noexcept {
            struct final_awaitable {
                coroutine_handle this_handle;

                bool await_ready() noexcept { return false; }

                void await_suspend(std::coroutine_handle<>) noexcept {
                    auto wchdl = core::worker::current().pop_handle();
                    asco_assert(this_handle == wchdl);
                    this_handle.destroy();
                    return;
                }

                void await_resume() noexcept {}
            };

            if (this->m_future_object->m_result_completed.load(std::memory_order::acquire)) {
                this->m_future_object->m_result_completed.store(true, std::memory_order::release);
            }
            return final_awaitable{this->m_future_object->m_this_handle};
        }
    };

    bool await_ready() noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle) noexcept {
        m_caller_handle = handle;
        core::worker::current().push_handle(m_this_handle);
    }

    output_type await_resume() {
        m_result_completed.load(std::memory_order::acquire);

        if (m_e_ptr) {
            std::rethrow_exception(m_e_ptr);
        }
        if constexpr (output_void) {
            return;
        } else {
            return std::move(*m_value.get());
        }
    }

    util::erased &bind_lambda(util::erased &&l) noexcept {
        m_bound_lambda = std::move(l);
        return m_bound_lambda;
    }

    future(const future &) = delete;
    future &operator=(const future &) = delete;

    future(future &&rhs) noexcept
            : m_promise_storage_ptr{rhs.m_promise_storage_ptr}
            , m_this_handle{rhs.m_this_handle}
            , m_e_ptr{rhs.m_e_ptr}
            , m_result_completed{rhs.m_result_completed.load(std::memory_order::relaxed)}
            , m_caller_handle{rhs.m_caller_handle}
            , m_bound_lambda{std::move(rhs.m_bound_lambda)} {
        *m_promise_storage_ptr = this;
    }

    future &operator=(future &&rhs) noexcept {
        if (this != &rhs) {
            this->~future();
            new (this) future(std::move(rhs));
        }
        return *this;
    }

    ~future() {
        if (m_result_completed.load(std::memory_order::acquire)) {
            if (!m_e_ptr) {
                if constexpr (!output_void) {
                    m_value.get()->~output_type();
                }
            }
        }
    }

private:
    future(std::coroutine_handle<promise_type> handle, future **promise_storage_ptr) noexcept
            : m_this_handle{handle}
            , m_promise_storage_ptr{promise_storage_ptr} {
        *promise_storage_ptr = this;
    }

    future **m_promise_storage_ptr{nullptr};

    std::coroutine_handle<promise_type> m_this_handle;

    std::exception_ptr m_e_ptr;
    [[no_unique_address]] util::raw_storage<output_type> m_value{};
    std::atomic_bool m_result_completed{false};

    std::coroutine_handle<> m_caller_handle;

    util::erased m_bound_lambda;
};

namespace detail {

template<typename Fn>
struct is_specialization_of_future_type {
private:
    template<util::types::move_secure T>
    static std::true_type test(future<T> *);

    template<typename T>
    static std::false_type test(T *);

public:
    static constexpr bool value = decltype(test(std::declval<Fn *>()))::value;
};

template<typename T>
constexpr bool is_specialization_of_future_type_v = is_specialization_of_future_type<T>::value;

};  // namespace detail

template<typename F>
concept future_type = detail::is_specialization_of_future_type_v<std::remove_cvref_t<F>>;

template<typename Fn, typename... Args>
concept async_function =
    std::invocable<Fn, Args...> && future_type<std::invoke_result_t<std::remove_cvref_t<Fn>, Args...>>;

};  // namespace asco
