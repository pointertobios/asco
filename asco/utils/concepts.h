// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_UTILS_CONCEPTS_H
#define ASCO_UTILS_CONCEPTS_H

#include <concepts>
#include <coroutine>

namespace asco {

template<typename T>
constexpr bool is_move_secure_v =
    (std::is_move_constructible_v<T> && std::is_move_assignable_v<T>) || std::is_integral_v<T>
    || std::is_floating_point_v<T> || std::is_pointer_v<T> || std::is_void_v<T>;

template<typename F>
concept is_future = requires(F f) {
    typename F::corohandle;
    typename F::promise_type;
    typename F::return_type;
    { f.await_ready() } -> std::same_as<bool>;
    { f.await_suspend(std::coroutine_handle<>{}) };
    { f.await_resume() } -> std::same_as<typename F::return_type>;
    { f.await() } -> std::same_as<typename F::return_type>;
    { f.abort() } -> std::same_as<void>;
} && requires(F::promise_type p) {
    { p.get_return_object() } -> std::same_as<F>;
    { p.initial_suspend() } -> std::same_as<std::suspend_always>;
    { p.return_value(std::declval<typename F::return_type>()) } -> std::same_as<void>;
    { p.final_suspend() };
    { p.unhandled_exception() } -> std::same_as<void>;
};

template<typename F, typename... Args>
concept is_async_function = is_future<std::invoke_result_t<F, Args...>> && requires(F f) {
    { f(std::declval<Args>()...) } -> std::same_as<std::invoke_result_t<F, Args...>>;
};

template<typename F, typename = void>
struct first_argument {
    using type = void;
};

template<typename R, typename... Args>
struct first_argument<std::function<R(Args...)>> {
    using type = std::tuple_element_t<0, std::tuple<Args...>>;
};

template<typename R, typename A1, typename... Rest>
struct first_argument<R (*)(A1, Rest...)> {
    using type = A1;
};

template<typename C, typename R, typename A1, typename... Rest>
struct first_argument<R (C::*)(A1, Rest...) const, void> {
    using type = A1;
};

template<typename C, typename R, typename A1, typename... Rest>
struct first_argument<R (C::*)(A1, Rest...), void> {
    using type = A1;
};

template<typename F>
struct first_argument<F, std::void_t<decltype(&F::operator())>> {
    using type = typename first_argument<decltype(&F::operator())>::type;
};

template<typename F>
using first_argument_t = typename first_argument<F>::type;

template<typename F>
concept is_exception_handler =
    std::invocable<F, std::exception_ptr>
    || (std::is_base_of_v<std::exception, std::remove_reference_t<first_argument_t<F>>>
        && std::invocable<F, first_argument_t<F>>);

};  // namespace asco

#endif
