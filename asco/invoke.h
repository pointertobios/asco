// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>

#include <asco/future.h>
#include <asco/utils/concepts.h>
#include <asco/utils/erased.h>

namespace asco::invoke {

using namespace concepts;

template<typename... Args, async_function<Args...> Fn>
constexpr auto co_invoke(Fn &&f, Args &&...args) {
    std::unique_ptr<utils::erased> fnp = std::make_unique<utils::erased>(std::forward<Fn>(f));
    auto task = fnp->get<Fn>()(std::forward<Args>(args)...);
    task.bind_lambda(std::move(fnp));
    return task;
}

};  // namespace asco::invoke

namespace asco {

using invoke::co_invoke;

};
