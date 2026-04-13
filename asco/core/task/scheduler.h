// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <tuple>

#include <asco/concurrency/hash_map.h>
#include <asco/core/task/execution_domain.h>
#include <asco/core/task/executor.h>
#include <asco/panic.h>

namespace asco::core::task {

class scheduler {
protected:
    using context = scheduler_context;

public:
    void bind_execution_domain(execution_domain &domain) {
        if (m_domain) {
            panic("asco::core::task::scheduler: 已绑定执行域");
        }
        m_domain = &domain;
    }

    execution_domain &get_execution_domain() const {
        if (!m_domain) {
            panic("asco::core::task::scheduler: 当前调度器未绑定执行域");
        }
        return *m_domain;
    }

    virtual void attach_execution(execution_id id) = 0;
    virtual void detach_suspended_execution(execution_id id) = 0;

    // `**强制性语义要求**`: 唤醒时要同时唤醒父 execution_domain 中的父 execution
    virtual void awake_execution(execution_id id) noexcept = 0;
    virtual void suspend_current(execution_id id) noexcept = 0;

    virtual std::tuple<execution_id, context &> schedule() = 0;

    virtual bool has_active_execution() = 0;
    virtual bool has_suspended_execution() = 0;

    virtual bool is_suspended(execution_id id) = 0;

protected:
    execution_domain *m_domain{nullptr};
};

};  // namespace asco::core::task
