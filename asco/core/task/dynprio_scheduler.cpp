// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/task/dynprio_scheduler.h>

#include <asco/core/task/execution_domain.h>
#include <asco/panic.h>
#include <asco/util/tsc.h>

namespace asco::core::task {

dynprio_scheduler::dynprio_context &
dynprio_scheduler::dynprio_context::operator=(dynprio_context &&rhs) noexcept {
    if (this != &rhs) {
        m_scheduler = rhs.m_scheduler;
        m_id = rhs.m_id;
        m_begin = rhs.m_begin;
    }
    return *this;
}

void dynprio_scheduler::dynprio_context::begin() noexcept { m_begin = util::get_tsc(); }

void dynprio_scheduler::dynprio_context::end(bool completed) noexcept {
    auto end = util::get_tsc();
    auto exec_time = end - m_begin;
    if ((m_scheduler->m_current_suspend && !m_scheduler->m_preawake_executions.remove(m_id)) || completed) {
        m_scheduler->m_suspended_executions.insert(m_id);
    } else {
        m_scheduler->m_suspended_executions.remove(m_id);
        m_scheduler->m_active_executions.lock()->push(
            prioritied_execution{m_id, m_scheduler->m_current_execution.priority + exec_time});
    }
    m_scheduler->m_current_suspend = false;
}

void dynprio_scheduler::attach_execution(execution_id id) {
    asco_assert(m_exec_ctx_map.insert(id, dynprio_context{this, id}));
    m_active_executions.lock()->push(prioritied_execution{id, 0});
}

void dynprio_scheduler::detach_suspended_execution(execution_id id) {
    if (m_suspended_executions.remove(id)) {
        m_exec_ctx_map.remove(id);
    }
}

void dynprio_scheduler::awake_execution(execution_id id) noexcept {
    if (m_suspended_executions.remove(id)) {
        m_active_executions.lock()->push({id, 0});
    } else {
        m_preawake_executions.insert(id);
    }
    auto parent_domain = m_domain->get_parent_domain();
    auto parent_exec = m_domain->get_parent_execution();
    if (parent_domain && parent_domain->get_scheduler().is_suspended(parent_exec)) {
        parent_domain->get_scheduler().awake_execution(parent_exec);
    }
}

void dynprio_scheduler::suspend_current(execution_id id) noexcept {
    asco_assert(m_current_execution.id == id);

    if (m_preawake_executions.remove(id)) {
        return;
    }

    m_current_suspend = true;
}

std::tuple<execution_id, dynprio_scheduler::context &> dynprio_scheduler::schedule() {
    auto g = m_active_executions.lock();
    asco_assert(!g->empty());

    m_current_execution = g->top();
    g->pop();

    return {m_current_execution.id, m_exec_ctx_map.get(m_current_execution.id).value()};
}

bool dynprio_scheduler::has_active_execution() {
    auto g = m_active_executions.lock();
    return !g->empty();
}

bool dynprio_scheduler::has_suspended_execution() {
    return m_suspended_executions.size() || m_current_suspend;
}

bool dynprio_scheduler::is_suspended(execution_id id) {
    return m_suspended_executions.contains(id) || (m_current_suspend && m_current_execution.id == id);
}

};  // namespace asco::core::task
