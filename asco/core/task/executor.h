// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <vector>

#include <asco/core/cancellation.h>
#include <asco/core/task/execution_domain.h>
#include <asco/util/erased.h>

namespace asco::core::task {

class scheduler_context {
public:
    virtual void begin() noexcept = 0;
    virtual void end(bool completed) noexcept = 0;
};

class executor final {
public:
    executor() = default;

    executor(const executor &) = delete;
    executor &operator=(const executor &) = delete;

    executor(executor &&) = delete;
    executor &operator=(executor &&) = delete;

    // 返回 false 表示当前执行流已结束
    bool execute(scheduled_execution exec, const std::vector<scheduler_context *> &ctxs);

    void push_handle(std::coroutine_handle<> handle);
    std::coroutine_handle<> pop_handle();

    std::coroutine_handle<> current_coroutine() const;
    execution_id current_execution() const;

    cancel_token &get_cancel_token() { return m_current_cancel_token; }
    void close_cancellation() noexcept;

private:
    execution_domain *m_domain{nullptr};
    execution_id m_current_id{};
    execution *m_execution{nullptr};
    cancel_token m_current_cancel_token;

    bool cancel_cleanup() noexcept;
};

};  // namespace asco::core::task
