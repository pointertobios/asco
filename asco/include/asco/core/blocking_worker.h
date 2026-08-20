// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "asco/concurrency/mpsc.h"
#include "asco/core/daemon.h"
#include "asco/core/predecl.h"

namespace asco::core {

class blocking_worker final : public daemon {
public:
    blocking_worker(
        runtime *rt, usize wid, task_receiver &&task_rx, concurrency::mpsc<usize>::sender &&acceptible_tx)
            : m_host_runtime{rt}
            , m_wid{wid}
            , m_task_rx{std::move(task_rx)}
            , m_acceptible_tx{std::move(acceptible_tx)} {}

private:
    bool initialize() override { return true; }
    bool run_once(std::stop_token &st) override;
    void finalize() override {}

    runtime *const m_host_runtime;
    const usize m_wid;
    task_receiver m_task_rx;
    concurrency::mpsc<usize>::sender m_acceptible_tx;

    task_id m_this_task;
};

};  // namespace asco::core
