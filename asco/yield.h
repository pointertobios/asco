// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_YIELD_H
#define ASCO_YIELD_H 1

#include <asco/future.h>

namespace asco {

template<typename R = RT>
struct yield {
    __asco_always_inline bool await_ready() { return false; }

    __asco_always_inline bool await_suspend(std::coroutine_handle<>) { return true; }

    __asco_always_inline void await_resume() {}
};

};  // namespace asco

#endif
