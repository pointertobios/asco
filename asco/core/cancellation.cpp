// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/cancellation.h>

#include <functional>
#include <stop_token>
#include <utility>

#include <asco/core/worker.h>
#include <asco/this_task.h>

namespace asco::core {

cancel_token cancel_source::get_token() noexcept { return cancel_token{*this, m_stop_source.get_token()}; }

void cancel_source::request_cancel() noexcept {
    m_stop_source.request_stop();
    // callback 需要在 worker 线程真正从 resume() 返回后执行，否则可能会破坏协程内变量的生命周期
}

void cancel_source::invoke_callbacks() noexcept {
    auto &w = worker::current();
    auto exec = w.get_executor().current_execution();
    auto guard = w.get_current_execution_domain().m_executions.get(exec);
    auto &callbacks = guard.value().cancel_callback_stack;
    while (callbacks.size() > 0) {
        callbacks.back()();
        callbacks.pop_back();
    }
}

cancel_token::cancel_token(cancel_source &source, std::stop_token token) noexcept
        : m_valid{true}
        , m_source{&source}
        , m_stop_token{std::move(token)} {}

bool cancel_token::cancel_requested() { return m_stop_token.stop_requested(); }

cancel_source *cancel_token::source() noexcept { return m_source; }

cancel_token::operator bool() const noexcept { return m_valid; }

cancel_callback::cancel_callback(std::function<void()> callback) noexcept
        : m_source{*this_task::get_current_cancel_token().m_source} {
    auto &w = worker::current();
    auto exec = w.get_executor().current_execution();
    auto guard = w.get_current_execution_domain().m_executions.get(exec);
    guard.value().cancel_callback_stack.push_back(callback);
}

cancel_callback::~cancel_callback() {
    auto &w = worker::current();
    auto exec = w.get_executor().current_execution();
    auto guard = w.get_current_execution_domain().m_executions.get(exec);
    auto &stack = guard.value().cancel_callback_stack;
    if (stack.size() > 0) {
        stack.pop_back();
    }
}

};  // namespace asco::core
