// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/yield.h>

#include <coroutine>

#include <asco/core/worker.h>

namespace asco::this_task {

std::suspend_always yield() {
    core::worker::current().yield_current();
    return {};
}

};  // namespace asco::this_task
