// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <deque>
#include <queue>

#include <asco/concurrency/hash_map.h>
#include <asco/core/task/execution_domain.h>
#include <asco/core/task/scheduler.h>
#include <asco/sync/spinlock.h>

namespace asco::core::task {

class dynprio_scheduler final : public scheduler {
    class dynprio_context : public context {
    public:
        dynprio_context(dynprio_scheduler *scheduler, execution_id id)
                : m_scheduler{scheduler}
                , m_id{id} {}

        dynprio_context(dynprio_context &&rhs) noexcept
                : m_scheduler{rhs.m_scheduler}
                , m_id{rhs.m_id}
                , m_begin{rhs.m_begin} {}

        dynprio_context &operator=(dynprio_context &&rhs) noexcept;

        void begin() noexcept override;
        void end(bool completed) noexcept override;

    private:
        dynprio_scheduler *m_scheduler;
        execution_id m_id;
        std::uint64_t m_begin;
    };

    struct prioritied_execution {
        execution_id id;
        std::uint64_t priority;

        bool operator<(const prioritied_execution &rhs) const { return priority > rhs.priority; }
    };

public:
    void attach_execution(execution_id id) override;
    void detach_suspended_execution(execution_id id) override;

    void awake_execution(execution_id id) noexcept override;
    void suspend_current(execution_id id) noexcept override;

    std::tuple<execution_id, context &> schedule() override;

    bool has_active_execution() override;
    bool has_suspended_execution() override;

private:
    prioritied_execution m_current_execution;
    bool m_current_suspend{false};

    sync::spinlock<std::priority_queue<prioritied_execution, std::deque<prioritied_execution>>>
        m_active_executions;
    concurrency::hash_set<execution_id> m_suspended_executions;
    concurrency::hash_set<execution_id> m_preawake_executions;

    concurrency::hash_map<execution_id, dynprio_context> m_exec_ctx_map;
};

};  // namespace asco::core::task
