// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/task/executor.h>

#include <algorithm>
#include <coroutine>
#include <ranges>
#include <vector>

#include <asco/panic.h>

namespace asco::core::task {

bool executor::execute(scheduled_execution exec, const std::vector<scheduler_context *> &ctxs) {
    m_domain = &exec.m_domain;
    m_current_id = exec.m_id;

    if (execution_guard().value().handle_stack.empty()) {
        m_domain = nullptr;
        return false;
    }

    m_current_cancel_token_stack =
        execution_guard().value().get_cancel_source_stack()
        | std::views::transform([](cancel_source *src) { return src->get_token(); })
        | std::ranges::to<std::vector<cancel_token>>();

    std::ranges::for_each(ctxs, [](scheduler_context *ctx) { ctx->begin(); });
    auto exit_stack = [&](bool completed) {
        std::ranges::for_each(ctxs | std::views::reverse, [&completed](scheduler_context *ctx) {
            ctx->end(completed);
            completed = false;  // 只有最内层的 ctx 的 completed 参数为 true
        });
        m_domain = nullptr;
    };

    if (cancel_cleanup()) {
        exit_stack(true);
        return false;
    }
    execution_guard().value().handle_stack.back().resume();
    if (cancel_cleanup()) {
        exit_stack(true);
        return false;
    }

    bool res = !execution_guard().value().handle_stack.empty();
    exit_stack(!res);
    return res;
}

void executor::push_handle(std::coroutine_handle<> handle) {
    execution_guard().value().handle_stack.push_back(handle);
    asco_assert(m_domain);
    m_domain->m_corohandle_exec_map.insert(
        handle, std::coroutine_handle{execution_guard().value().handle_stack.front()});
}

std::coroutine_handle<> executor::pop_handle() {
    if (execution_guard().value().handle_stack.empty()) {
        return {};
    }
    auto hdl = execution_guard().value().handle_stack.back();
    execution_guard().value().handle_stack.pop_back();
    asco_assert(m_domain);
    m_domain->m_corohandle_exec_map.remove(hdl);
    return hdl;
}

std::coroutine_handle<> executor::current_coroutine() {
    if (execution_guard().value().handle_stack.empty()) {
        return {};
    }
    return execution_guard().value().handle_stack.back();
}

execution_id executor::current_execution() const {
    if (!m_domain) {
        return {};
    }
    return m_current_id;
}

bool executor::is_base_coroutine(std::coroutine_handle<> handle) {
    return execution_guard().value().handle_stack.size()
           && execution_guard().value().handle_stack.front() == handle;
}

std::span<cancel_source *> executor::current_cancel_source_stack() {
    return execution_guard().value().get_cancel_source_stack();
}

bool executor::cancel_cleanup() noexcept {
    if (std::ranges::all_of(
            m_current_cancel_token_stack, [](cancel_token &token) { return !token.cancel_requested(); })) {
        return false;
    }

    auto s = m_current_cancel_token_stack.back().source();
    s->invoke_callbacks();
    while (execution_guard().value().handle_stack.size()) {
        auto h = pop_handle();
        h.destroy();
    }
    return true;
}

};  // namespace asco::core::task
