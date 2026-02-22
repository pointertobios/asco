// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/this_task.h>

#include <asco/core/cancellation.h>
#include <asco/core/runtime.h>
#include <asco/panic.h>

namespace asco::this_task {

void close_cancellation() noexcept {
    if (!in_runtime()) {
        panic("asco::this_task::stop_cancellation: 不在 runtime 中");
    }
    auto &worker = core::worker::current();
    if (worker.m_current_stack.size()) {
        worker.close_cancellation(worker.m_current_stack.front());
    } else {
        panic("asco::this_task::stop_cancellation: 当前没有正在运行的任务");
    }
}

core::cancel_token &get_cancel_token() noexcept {
    if (!in_runtime()) {
        panic("asco::this_task::get_cancel_token: 不在 runtime 中");
    }
    auto &worker = core::worker::current();
    if (worker.m_current_stack.size()) {
        return worker.m_current_cancel_token;
    } else {
        panic("asco::this_task::get_cancel_token: 当前没有正在运行的任务");
    }
}

};  // namespace asco::this_task
