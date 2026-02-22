// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/wait_for_valid.h>

#include <asco/core/worker.h>
#include <asco/future.h>
#include <asco/yield.h>

namespace asco::core {

future<void> wait_for_valid(std::coroutine_handle<> handle) {
    while (!worker::handle_valid(handle)) {
        co_await this_task::yield();
    }
}

};  // namespace asco::core
