// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <coroutine>

#include <asco/utils/concepts.h>
#include <asco/utils/types.h>

namespace asco {

template<concepts::move_secure T = void>
struct yield;

template<>
struct yield<void> : std::suspend_always {};

template<concepts::move_secure T>
struct yield : std::suspend_always {
    using deliver_type = void;

    T value;

    yield(T &&v) noexcept
            : value{std::move(v)} {}

    utils::passing_ref<T> get_value() noexcept { return value; }

    T await_resume() { return std::move(value); }
};

struct noop : std::suspend_never {};

};  // namespace asco
