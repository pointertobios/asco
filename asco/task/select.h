// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <expected>
#include <limits>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include <asco/core/task/cycle_scheduler.h>
#include <asco/core/task/execution_domain_proxy.h>
#include <asco/future.h>
#include <asco/invoke.h>
#include <asco/this_task.h>

namespace asco::task {

template<async_function... Args>
class select {
    template<std::size_t I, typename T>
        requires(I < sizeof...(Args))
    struct result_branch {
        static constexpr std::size_t index = I;
        using type = T;

        T value;
    };

    template<std::size_t... I>
    static auto make_result_type(std::index_sequence<I...>) -> std::variant<result_branch<
        I, std::expected<typename std::invoke_result_t<Args>::output_type, std::exception_ptr>>...>;

    using result_type = decltype(make_result_type(std::index_sequence_for<Args...>{}));

public:
    select(Args &&...args)
            : m_futures{make_futures(std::index_sequence_for<Args...>{}, std::forward<Args>(args)...)} {
        std::apply([&](auto &...args) { (m_domain.attach_execution(args.as_execution()), ...); }, m_futures);
    }

    bool await_ready() noexcept { return false; }

    void await_suspend(std::coroutine_handle<>) noexcept {}

    result_type await_resume() noexcept {
        auto branch = m_complete_branch.load(std::memory_order_acquire);
        asco_assert(branch != std::numeric_limits<std::size_t>::max());

        auto gen = [branch,
                    &m_futures = m_futures]<std::size_t I, typename... Ts>() -> std::optional<result_type> {
            if (branch == I) {
                using output_type =
                    typename std::invoke_result_t<std::tuple_element_t<I, std::tuple<Ts...>>>::output_type;

                try {
                    if constexpr (std::is_void_v<output_type>) {
                        std::get<I>(m_futures).await_resume();
                        return result_type{
                            std::in_place_type<result_branch<I, std::expected<output_type, std::exception_ptr>>>,
                            std::expected<output_type, std::exception_ptr>{}};
                    } else {
                        return result_type{
                            std::in_place_type<result_branch<I, std::expected<output_type, std::exception_ptr>>>,
                            std::expected<output_type, std::exception_ptr>{
                                std::get<I>(m_futures).await_resume()}};
                    }
                } catch (...) {
                    return result_type{
                        std::in_place_type<result_branch<I, std::expected<output_type, std::exception_ptr>>>,
                        std::expected<output_type, std::exception_ptr>{
                            std::unexpected{std::current_exception()}}};
                }
            } else {
                return std::nullopt;
            }
        };
        return [&gen]<std::size_t... I>(std::index_sequence<I...>) {
            std::optional<result_type> res;
            ((res = gen.template operator()<I, Args...>()) || ...);
            return std::move(*res);
        }(std::index_sequence_for<Args...>{});
    }

private:
    std::tuple<std::invoke_result_t<Args>...> m_futures;
    std::atomic_size_t m_complete_branch{std::numeric_limits<std::size_t>::max()};

    core::task::cycle_scheduler m_scheduler{};
    core::task::execution_domain_proxy m_domain{m_scheduler};

    template<std::size_t... I>
        requires(sizeof...(I) == sizeof...(Args) && ((I < sizeof...(Args)) && ...))
    auto make_futures(std::index_sequence<I...>, Args &&...args) {
        return std::make_tuple(select_impl<I>(co_invoke(std::forward<Args>(args)))...);
    }

    template<std::size_t I, future_type Future>
    future<typename Future::output_type> select_impl(Future fut) {
        using output_type = typename Future::output_type;

        try {
            if constexpr (std::is_void_v<output_type>) {
                co_await fut;
                if (std::size_t e = std::numeric_limits<std::size_t>::max();
                    m_complete_branch.compare_exchange_strong(
                        e, I, std::memory_order::acq_rel, std::memory_order::relaxed)) {
                    m_domain.cancel();
                    co_return;
                }
            } else {
                auto res = co_await fut;
                if (std::size_t e = std::numeric_limits<std::size_t>::max();
                    m_complete_branch.compare_exchange_strong(
                        e, I, std::memory_order::acq_rel, std::memory_order::relaxed)) {
                    m_domain.cancel();
                    co_return std::move(res);
                }
            }
        } catch (...) {
            if (std::size_t e = std::numeric_limits<std::size_t>::max();
                m_complete_branch.compare_exchange_strong(
                    e, I, std::memory_order::acq_rel, std::memory_order::relaxed)) {
                m_domain.cancel();
                std::rethrow_exception(std::current_exception());
            }
        }
        // 应总是被成功取消，应该永远不能返回或抛异常
        while (true) {
            co_await this_task::yield();
        }
    }
};

template<typename... Args>
select(Args &&...) -> select<std::remove_cvref_t<Args>...>;

};  // namespace asco::task
