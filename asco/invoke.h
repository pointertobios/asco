// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>

#include <asco/future.h>
#include <asco/join_handle.h>
#include <asco/util/erased.h>

namespace asco {

template<typename... Args, typename Fn>
    requires(
        (async_function<Fn, Args...> || spawned_function<Fn, Args...>)
        && !std::is_function_v<std::remove_cvref_t<Fn>>)
constexpr auto co_invoke(Fn &&f, Args &&...args) {
    if constexpr (std::is_rvalue_reference_v<decltype(f)>) {
        using FnType = std::remove_cvref_t<Fn>;
        util::erased fnp = util::erased{std::forward<FnType>(f)};
        auto task = std::invoke(fnp.get<FnType>(), std::forward<Args>(args)...);
        task.bind_lambda(std::move(fnp));
        return task;
    } else {
        return std::invoke(f, std::forward<Args>(args)...);
    }
}

template<typename... Args, typename Fn>
    requires(
        (async_function<Fn, Args...> || spawned_function<Fn, Args...>)
        && std::is_function_v<std::remove_cvref_t<Fn>>)
constexpr auto co_invoke(Fn &&f, Args &&...args) {
    return std::invoke(f, std::forward<Args>(args)...);
}

};  // namespace asco
