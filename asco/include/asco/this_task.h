// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "asco/core/runtime.h"

namespace asco::this_task {

inline core::runtime &host_runtime() { return core::worker::current().host_runtime(); }

template<typename... Args>
auto spawn(async_function<Args...> auto &&fn, Args &&...args) {
    return core::worker::current().host_runtime().spawn(
        std::forward<decltype(fn)>(fn), std::forward<Args>(args)...);
}

template<typename... Args>
auto spawn_blocking(std::invocable<Args...> auto &&fn, Args &&...args)
    requires(!async_function<decltype(fn), Args...>)
{
    return core::worker::current().host_runtime().spawn_blocking(
        std::forward<decltype(fn)>(fn), std::forward<Args>(args)...);
}

inline auto yield() {
    struct yield_awaitable {
        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> handle) {
            auto &w = core::worker::current();
            w.set_next_resume(handle);
            w.set_yield_now();
        }

        void await_resume() const noexcept {}
    };
    return yield_awaitable{};
}

};  // namespace asco::this_task
