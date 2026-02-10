// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>

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
        std::unique_ptr<util::erased> fnp = std::make_unique<util::erased>(std::forward<FnType>(f));
        auto task = fnp->get<FnType>()(std::forward<Args>(args)...);
        task.bind_lambda(std::move(fnp));
        return task;
    } else {
        return f(std::forward<Args>(args)...);
    }
}

template<typename... Args, typename Fn>
    requires(
        (async_function<Fn, Args...> || spawned_function<Fn, Args...>)
        && std::is_function_v<std::remove_cvref_t<Fn>>)
constexpr auto co_invoke(Fn &&f, Args &&...args) {
    return f(std::forward<Args>(args)...);
}

};  // namespace asco
