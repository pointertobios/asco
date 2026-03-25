// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/task/executor.h>

namespace asco::core::task {

bool executor::execute(scheduled_execution exec, scheduler_context &ctx) {
    m_domain = &exec.m_domain;
    m_current_id = exec.m_id;
    m_execution = exec.m_exec;

    if (m_execution->handle_stack.empty()) {
        return false;
    }

    ctx.begin();

    m_current_cancel_token = m_execution->cancel_src ? m_execution->cancel_src->get_token() : cancel_token{};

    if (cancel_cleanup()) {
        ctx.end(true);
        m_domain = nullptr;
        m_execution = nullptr;
        return false;
    }
    m_execution->handle_stack.back().resume();
    if (cancel_cleanup()) {
        ctx.end(true);
        m_domain = nullptr;
        m_execution = nullptr;
        return false;
    }

    bool res = !m_execution->handle_stack.empty();
    ctx.end(!res);
    m_domain = nullptr;
    m_execution = nullptr;
    return res;
}

void executor::push_handle(std::coroutine_handle<> handle) {
    asco_assert(m_execution);
    m_execution->handle_stack.push_back(handle);
    asco_assert(m_domain);
    m_domain->m_corohandle_exec_map.insert(handle, std::coroutine_handle{m_execution->handle_stack.front()});
}

std::coroutine_handle<> executor::pop_handle() {
    asco_assert(m_execution);
    if (m_execution->handle_stack.empty()) {
        return {};
    }
    auto hdl = m_execution->handle_stack.back();
    m_execution->handle_stack.pop_back();
    asco_assert(m_domain);
    m_domain->m_corohandle_exec_map.remove(hdl);
    return hdl;
}

std::coroutine_handle<> executor::current_coroutine() const {
    if (!m_execution || m_execution->handle_stack.empty()) {
        return {};
    }
    return m_execution->handle_stack.back();
}

execution_id executor::current_execution() const {
    if (!m_execution) {
        return {};
    }
    return m_current_id;
}

void executor::close_cancellation() noexcept {
    if (m_current_cancel_token) {
        m_current_cancel_token.close_cancellation();
    }
}

bool executor::cancel_cleanup() noexcept {
    if (!m_execution->handle_stack.size() ||  //
        !m_current_cancel_token || !m_current_cancel_token.cancel_requested()) {
        return false;
    }

    if (m_current_cancel_token.cancellation_closed()) {
        panic(
            "asco::worker: 外部取消了已被关闭取消的执行流 {{{}}}",
            m_execution->handle_stack.front().address());
    }

    auto s = m_current_cancel_token.source();
    s->invoke_callbacks();
    while (m_execution->handle_stack.size()) {
        auto h = pop_handle();
        h.destroy();
    }
    return true;
}

};  // namespace asco::core::task
