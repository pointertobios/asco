// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/yield.h>

#include <coroutine>

#include <asco/core/worker.h>

namespace asco::this_task {

std::suspend_always yield() {
    auto &w = core::worker::current();
    if (auto g = w.m_active_stacks.lock(); !w.m_current_stack.empty()) {
        g->emplace_back(std::move(w.m_current_stack));
    }
    return {};
}

};  // namespace asco::this_task
