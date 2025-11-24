// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <coroutine>

#include <asco/utils/concepts.h>

namespace asco {

template<concepts::move_secure T = void>
struct yield;

template<>
struct yield<void> : std::suspend_always {};

template<concepts::move_secure T>
struct yield : std::suspend_always {
    T value;

    yield(T &&v) noexcept
            : value{std::move(v)} {}

    T await_resume() { return std::move(value); }
};

};  // namespace asco
