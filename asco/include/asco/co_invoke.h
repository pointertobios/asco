// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <type_traits>
#include <utility>

#include "asco/future.h"
#include "asco/join_handle.h"
#include "asco/types/erased.h"

namespace asco {

template<typename Fn, typename... Args>
concept async_function = requires(Fn fn, Args... args) {
    requires requires {
        { fn(args...) } -> future_type;
    } || requires {
        { fn(args...) } -> join_handle_type;
    };
};

template<typename... Args, async_function<Args...> Fn>
    requires(!std::is_function_v<Fn>)
constexpr auto co_invoke(Fn &&fn, Args &&...args) noexcept(std::is_nothrow_invocable_v<Fn, Args...>) {
    if constexpr (std::is_rvalue_reference_v<decltype(fn)>) {
        using FnType = std::remove_cvref_t<Fn>;
        types::erased fnp = types::erased::create<FnType>(std::forward<FnType>(fn));
        auto future_value = std::invoke(fnp.get<FnType>(), std::forward<Args>(args)...);
        future_value.bind_invocable(std::move(fnp));
        return future_value;
    } else {
        return std::invoke(fn, std::forward<Args>(args)...);
    }
}

template<typename... Args, async_function<Args...> Fn>
    requires(std::is_function_v<Fn>)
constexpr auto co_invoke(Fn &&fn, Args &&...args) noexcept(std::is_nothrow_invocable_v<Fn, Args...>) {
    return std::invoke(fn, std::forward<Args>(args)...);
}

};  // namespace asco
