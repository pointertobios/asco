// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <coroutine>
#include <vector>

#include <asco/concurrency/hash_map.h>
#include <asco/core/cancellation.h>
#include <asco/util/raw_storage.h>

namespace asco::core::task {

class execution_domain;

using execution_id = std::coroutine_handle<>;

struct execution {
    std::vector<std::coroutine_handle<>> handle_stack;
    cancel_source *cancel_src;

    execution(execution_id id, cancel_source *src);

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
};

class execution_domain final {
    friend class executor;

public:
    explicit execution_domain() = default;

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

private:
    concurrency::hash_map<execution_id, execution> m_executions;
    concurrency::hash_map<std::coroutine_handle<>, execution_id> m_corohandle_exec_map;
};

};  // namespace asco::core::task
