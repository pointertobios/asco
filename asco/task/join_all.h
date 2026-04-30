// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <coroutine>
#include <exception>
#include <expected>
#include <tuple>
#include <type_traits>
#include <utility>

#include <asco/core/task/cycle_scheduler.h>
#include <asco/core/task/execution_domain_proxy.h>
#include <asco/future.h>
#include <asco/invoke.h>
#include <asco/util/types.h>

namespace asco::task {

template<async_function... Args>
class join_all {
    template<typename Future>
    using result_type = std::expected<
        util::types::monostate_if_void<typename std::remove_cvref_t<Future>::output_type>,
        std::exception_ptr>;

public:
    join_all(Args &&...args)
            : m_futures{std::make_tuple(co_invoke(std::forward<Args>(args))...)} {
        std::apply([&](auto &...args) { (m_domain.attach_execution(args.as_execution()), ...); }, m_futures);
    }

    bool await_ready() noexcept { return false; }

    void await_suspend(std::coroutine_handle<>) noexcept {}

    auto await_resume() noexcept {
        return std::apply(
            [](auto &...args) {
                return std::make_tuple([](auto &future) -> result_type<decltype(future)> {
                    using future_type = std::remove_cvref_t<decltype(future)>;
                    try {
                        if constexpr (future_type::output_void) {
                            future.await_resume();
                            return std::monostate{};
                        } else {
                            return future.await_resume();
                        }
                    } catch (...) { return std::unexpected{std::current_exception()}; }
                }(args)...);
            },
            m_futures);
    }

private:
    std::tuple<std::invoke_result_t<Args>...> m_futures;

    core::task::cycle_scheduler m_scheduler{};
    core::task::execution_domain_proxy m_domain{m_scheduler};
};

template<typename... Args>
join_all(Args &&...) -> join_all<std::remove_cvref_t<Args>...>;

};  // namespace asco::task
