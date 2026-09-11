// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include "asco/future.h"

#ifdef ASCO_DEBUG_ENABLED

#    include "asco/this_task.h"

namespace asco::detail {

void future_trace_start(std::source_location sl) {
    auto &dh = this_task::host_runtime().get_debug_host();
    dh.trace_coroutine_start(this_task::worker().id(), this_task::id(), sl);
}

void future_trace_end() {
    auto &dh = this_task::host_runtime().get_debug_host();
    dh.trace_coroutine_end(this_task::worker().id(), this_task::id());
}

void future_create_placement_executing_guard(types::raw_storage<core::placement_executing_guard> &guard) {
    if (core::worker::exists()) {
        guard.emplace(this_task::worker().placement_executing());
    }
}

void future_destroy_placement_executing_guard(types::raw_storage<core::placement_executing_guard> &guard) {
    if (core::worker::exists()) {
        guard.destroy();
    }
}

};  // namespace asco::detail

#endif
