// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_SUSPEND_H
#define ASCO_SUSPEND_H 1

#include <cstddef>

#include <asco/future.h>

namespace asco {

template<typename R = RT>
struct suspend {
    __always_inline bool await_ready() { return false; }

    __always_inline bool await_suspend(std::coroutine_handle<> handle) { return true; }

    __always_inline void await_resume() {}
};

};  // namespace asco

#endif
