// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include "asco/debug/debug_host.h"

#include <concepts>
#include <format>

namespace asco::debug {

debug_host::debug_host()
        : daemon{"asco::dbhost"}
        , m_is_client{false} {
    std::tie(m_cmd_tx, m_cmd_rx) = concurrency::mpsc<command>::queue();
}

debug_host::debug_host(client_mode_tag)
        : daemon{"asco::dbhost"}
        , m_is_client{true} {}

std::unique_ptr<debug_host> debug_host::create() { return std::make_unique<debug_host>(); }

std::unique_ptr<debug_host> debug_host::attach() { return std::make_unique<debug_host>(client_mode_tag{}); }

void debug_host::trace_coroutine_start(usize worker, task_id tid, std::source_location sl) {
    while (!m_cmd_tx.send(coroutine_start{worker, tid, sl})) {}
    awake();
}

void debug_host::trace_coroutine_end(usize worker, task_id tid) {
    while (!m_cmd_tx.send(coroutine_end{worker, tid})) {}
    awake();
}

bool debug_host::run_once(std::stop_token &st) {
    wait_for_awake();
    while (true) {
        auto cmd = m_cmd_rx.recv();
        if (!cmd.has_value()) {
            if (cmd.error() == concurrency::receive_failed::empty) {
                break;
            } else {
                continue;
            }
        }

        std::visit(
            [&tasks = this->m_tasks, &task_map = this->m_task_map](auto &cmd) {
                using cmd_type = std::remove_cvref_t<decltype(cmd)>;
                if constexpr (std::same_as<cmd_type, coroutine_start>) {
                    task_map[cmd.w][cmd.t].push_back(
                        std::format(
                            "{} ({}:{}:{})", cmd.sl.function_name(), cmd.sl.file_name(), cmd.sl.line(),
                            cmd.sl.column()));
                } else if constexpr (std::same_as<cmd_type, coroutine_end>) {
                    if (auto &v = task_map[cmd.w][cmd.t]; !v.empty()) {
                        v.pop_back();
                        if (v.size() == 0) {
                            task_map[cmd.w].erase(cmd.t);
                        }
                    }
                }
            },
            cmd.value());
    }

    return !st.stop_requested();
}

};  // namespace asco::debug
