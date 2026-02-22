// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>
#include <stop_token>
#include <vector>

#include <asco/sync/spinlock.h>

namespace asco::core {

class coroutine_cancelled final {};

class cancel_source;
class cancel_token;
class cancel_callback;

class cancel_source final {
    friend class cancel_token;
    friend class cancel_callback;

public:
    cancel_source() = default;

    cancel_token get_token() noexcept;

    void request_cancel() noexcept;

    void invoke_callbacks() noexcept;

private:
    std::stop_source m_stop_source{};
    sync::spinlock<std::vector<std::function<void()>>> m_callbacks{};

    std::atomic_bool m_closed{false};
};

class cancel_token final {
    friend class cancel_source;
    friend class cancel_callback;

public:
    cancel_token() = default;

    bool cancel_requested();

    void close_cancellation() noexcept;
    bool cancellation_closed() const noexcept;
    cancel_source *source() noexcept;

    operator bool() const noexcept;

private:
    cancel_token(cancel_source &source, std::stop_token token) noexcept;

    bool m_valid{false};
    cancel_source *m_source{nullptr};
    std::stop_token m_stop_token;
};

class cancel_callback final {
public:
    cancel_callback(cancel_token &token, std::function<void()> callback) noexcept;
    ~cancel_callback();

    cancel_callback(const cancel_callback &) = delete;
    cancel_callback &operator=(const cancel_callback &) = delete;

    cancel_callback(cancel_callback &&) = delete;
    cancel_callback &operator=(cancel_callback &&) = delete;

private:
    cancel_source &m_source;
};

};  // namespace asco::core
