// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#include "asco/concurrency/mpsc.h"
#include "asco/core/daemon.h"
#include "asco/core/worker.h"

namespace asco::debug {

class debug_host final : public core::daemon {
    using task_id = core::task_id;

    struct coroutine_start {
        usize w;
        task_id t;
        std::source_location sl;
    };
    struct coroutine_end {
        usize w;
        task_id t;
    };
    using command = std::variant<coroutine_start, coroutine_end>;

public:
    debug_host();

    struct client_mode_tag {};
    debug_host(client_mode_tag);

    static std::unique_ptr<debug_host> create();
    static std::unique_ptr<debug_host> attach();

    void trace_coroutine_start(usize worker, task_id tid, std::source_location sl);
    void trace_coroutine_pause(usize worker, task_id tid);
    void trace_coroutine_continue(usize worker, task_id tid, std::source_location sl);
    void trace_coroutine_end(usize worker, task_id tid);

private:
    bool initialize() override { return true; }
    bool run_once(std::stop_token &st) override;
    void finalize() override {}

    bool m_is_client;

    std::unordered_set<task_id> m_tasks;
    std::unordered_map<usize, std::unordered_map<task_id, std::vector<std::string>>> m_task_map;
    concurrency::mpsc<command>::sender m_cmd_tx;
    concurrency::mpsc<command>::receiver m_cmd_rx;
};

};  // namespace asco::debug
