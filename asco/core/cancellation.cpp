// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/cancellation.h>
#include <atomic>

namespace asco::core {

cancel_token cancel_source::get_token() noexcept { return cancel_token{*this, m_stop_source.get_token()}; }

void cancel_source::request_cancel() noexcept {
    m_stop_source.request_stop();
    // callback 需要在 worker 线程真正从 resume() 返回后执行，否则可能会破坏协程内变量的声明周期
}

void cancel_source::invoke_callbacks() noexcept {
    auto callbacks = m_callbacks.lock();
    while (callbacks->size() > 0) {
        callbacks->back()();
        callbacks->pop_back();
    }
}

cancel_token::cancel_token(cancel_source &source, std::stop_token token) noexcept
        : m_valid{true}
        , m_source{&source}
        , m_stop_token{std::move(token)} {}

bool cancel_token::cancel_requested() { return m_stop_token.stop_requested(); }

void cancel_token::close_cancellation() noexcept {
    m_source->m_closed.store(true, std::memory_order::release);
}

bool cancel_token::cancellation_closed() const noexcept {
    return m_source->m_closed.load(std::memory_order::acquire);
}

cancel_source *cancel_token::source() noexcept { return m_source; }

cancel_token::operator bool() const noexcept { return m_valid; }

cancel_callback::cancel_callback(cancel_token &token, std::function<void()> callback) noexcept
        : m_source{*token.m_source} {
    m_source.m_callbacks.lock()->emplace_back(std::move(callback));
}

cancel_callback::~cancel_callback() {
    if (auto g = m_source.m_callbacks.lock(); g->size()) {
        g->pop_back();
    }
}

};  // namespace asco::core
