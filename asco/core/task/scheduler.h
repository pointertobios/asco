// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <tuple>

#include <asco/concurrency/hash_map.h>
#include <asco/core/task/execution_domain.h>
#include <asco/core/task/executor.h>

namespace asco::core::task {

class scheduler {
protected:
    using context = scheduler_context;

public:
    virtual void attach_execution(execution_id id) = 0;
    virtual void detach_suspended_execution(execution_id id) = 0;

    virtual void awake_execution(execution_id id) noexcept = 0;
    virtual void suspend_current(execution_id id) noexcept = 0;

    virtual std::tuple<execution_id, context &> schedule() = 0;
    virtual bool has_active_execution() = 0;
    virtual bool has_suspended_execution() = 0;

protected:
};

};  // namespace asco::core::task
