// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <coroutine>
#include <vector>

#include <asco/concurrency/hash_map.h>
#include <asco/core/cancellation.h>
#include <asco/util/raw_storage.h>

namespace asco::core::task {

class scheduler;

class execution_domain;

using execution_id = std::coroutine_handle<>;

struct execution {
    std::vector<std::coroutine_handle<>> handle_stack;
    cancel_source *cancel_src;
    execution_domain *subdomain;  // 此 subdomain 所有权必须在顶层 coroutine_handle，
                                  // 应在顶层 coroutine_handle 持有的 subdomain 销毁时将此指针置回 nullptr。
                                  // 此指针为非 nullptr 时， executor 直接进入 subdomain 调度

    execution(execution_id id, cancel_source *src);
    ~execution();

    execution(const execution &) = delete;
    execution &operator=(const execution &) = delete;

    execution(execution &&rhs) noexcept;
    execution &operator=(execution &&rhs) noexcept;

private:
    util::raw_storage<cancel_source> cancel_src_storage;
    bool cancel_src_owned;
};

struct scheduled_execution {
    execution_domain &m_domain;
    const execution_id m_id;
    execution *m_exec;

    execution_domain *get_subdomain() const { return m_exec->subdomain; }
};

class execution_domain final {
    friend class executor;

public:
    explicit execution_domain(scheduler *sched)
            : m_scheduler{sched} {}

    execution_domain(const execution_domain &) = delete;
    execution_domain &operator=(const execution_domain &) = delete;

    execution_domain(execution_domain &&) = delete;
    execution_domain &operator=(execution_domain &&) = delete;

    void attach_execution(execution_id id);
    void attach_execution(execution_id id, cancel_source *cancel_src);
    void detach_execution(execution_id id);

    scheduled_execution schedule_execution(execution_id id);

    std::coroutine_handle<> top_of_execution(execution_id id);
    execution_id execution_of_coroutine(std::coroutine_handle<> handle);

    bool is_empty() const { return !m_executions.size(); }

    scheduler &get_scheduler() const { return *m_scheduler; }

private:
    concurrency::hash_map<execution_id, execution> m_executions;
    concurrency::hash_map<std::coroutine_handle<>, execution_id> m_corohandle_exec_map;

    scheduler *m_scheduler;
};

};  // namespace asco::core::task
