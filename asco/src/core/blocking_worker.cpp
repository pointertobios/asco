// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include "asco/core/blocking_worker.h"

#include "asco/core/worker.h"

namespace asco::core {

bool blocking_worker::run_once(std::stop_token &st) {
    while (!m_acceptible_tx.send(m_wid)) {}

    auto task = m_task_rx.recv();
    if (!task.has_value() && task.error() == concurrency::receive_failed::empty && !st.stop_requested()) {
        wait_for_awake();
    }

    if (task.has_value()) {
        m_this_task = task->m_task_block->to_task_id();
        task->m_root_coroutine.resume();
    }

    return task || task.error() == concurrency::receive_failed::pending || !st.stop_requested();
}

};  // namespace asco::core
