// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_UTILS_CONCEPTS_H
#define ASCO_UTILS_CONCEPTS_H

#include <concepts>
#include <coroutine>

namespace asco {

template<typename T>
constexpr bool is_move_secure_v = 
    (std::is_move_constructible_v<T> && std::is_move_assignable_v<T>)
        || std::is_integral_v<T> || std::is_floating_point_v<T>
        || std::is_pointer_v<T> || std::is_void_v<T>;


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
    { p.final_suspend() } -> std::same_as<std::suspend_always>;
    { p.unhandled_exception() } -> std::same_as<void>;
};

};

#endif
