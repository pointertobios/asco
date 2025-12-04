// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>

#include <asco/future.h>
#include <asco/utils/concepts.h>
#include <asco/utils/erased.h>
#include <type_traits>

namespace asco::invoke {

using namespace concepts;

template<typename... Args, async_function<Args...> Fn>
    requires(!std::is_function_v<std::remove_cvref_t<Fn>>)
constexpr auto co_invoke(Fn &&f, Args &&...args) {
    if constexpr (std::is_rvalue_reference_v<decltype(f)>) {
        using FnType = std::remove_cvref_t<Fn>;
        std::unique_ptr<utils::erased> fnp = std::make_unique<utils::erased>(
            std::forward<FnType>(f), +[](void *p) noexcept { reinterpret_cast<FnType *>(p)->~FnType(); });
        auto task = fnp->get<FnType>()(std::forward<Args>(args)...);
        task.bind_lambda(std::move(fnp));
        return task;
    } else {
        return f(std::forward<Args>(args)...);
    }
}

template<typename... Args, async_function<Args...> Fn>
    requires(std::is_function_v<std::remove_cvref_t<Fn>>)
constexpr auto co_invoke(Fn &&f, Args &&...args) {
    return f(std::forward<Args>(args)...);
}

};  // namespace asco::invoke

namespace asco {

using invoke::co_invoke;

};
