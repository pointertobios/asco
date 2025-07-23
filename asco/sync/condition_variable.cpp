// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/sync/condition_variable.h>

namespace asco::sync {

void condition_variable::notify_one() {
    auto guard = waiting_tasks.lock();
    if (!guard->empty()) {
        auto id = std::move(guard->front());
        guard->pop_front();

        RT::get_runtime().awake(id);
    }
}

void condition_variable::notify_all() {
    auto guard = waiting_tasks.lock();
    for (auto id : *guard) { RT::get_runtime().awake(id); }
    guard->clear();
}

};  // namespace asco::sync
