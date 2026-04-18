// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <asco/core/task/execution_domain.h>
#include <asco/core/task/scheduler.h>
#include <asco/core/worker.h>

namespace asco::core::task {

class execution_domain_proxy {
public:
    execution_domain_proxy(scheduler &sched)
            : m_domain{sched} {
        sched.bind_execution_domain(m_domain);
        auto &w = worker::current();
        auto exec = w.get_executor().current_execution();
        auto &current_domain = w.get_current_execution_domain();
        current_domain.m_executions.get(exec).value().subdomain = &m_domain;
        m_domain.set_parent_domain(current_domain);
        m_domain.set_parent_execution(exec);
    }

    ~execution_domain_proxy() {}  // 通过空子执行域协议被 worker 自动从父 execution 移除

    void attach_execution(execution_id id) {
        auto &w = core::worker::current();
        auto cancel_src = w.get_executor().current_cancel_source();
        m_domain.attach_execution(id, cancel_src);
        m_domain.get_scheduler().attach_execution(id);
    }

    execution_domain &get_domain() { return m_domain; }

private:
    execution_domain m_domain;
};

};  // namespace asco::core::task
