// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <coroutine>
#include <unordered_map>
#include <vector>

#include <asco/core/worker.h>
#include <asco/sync/rwspin.h>
#include <asco/utils/defines.h>
#include <asco/utils/types.h>

namespace asco::core {

using namespace asco::types;

class runtime {
public:
    static runtime &init(size_t parallel = 0) noexcept;
    static runtime &this_runtime() noexcept;

    asco_always_inline task_id alloc_task_id() noexcept {
        return task_id_generator.fetch_add(1, morder::acq_rel);
    }

    void register_task(task_id id, std::shared_ptr<task<>> task);
    void unregister_task(task_id id);
    std::shared_ptr<task<>> get_task_by(std::coroutine_handle<> handle);
    std::shared_ptr<task<>> get_task_by(task_id id);
    task_id get_task_id_by(std::coroutine_handle<> handle);

    void spawn_task(task_id id, std::shared_ptr<task<>> task);
    void spawn_core_task(task_id id, std::shared_ptr<task<>> task);

    void awake_all();
    void awake_io_worker_once();
    void awake_calcu_worker_once();

private:
    explicit runtime(size_t parallel = 0);
    ~runtime();

    rwspin<std::unordered_map<task_id, std::shared_ptr<task<>>>> tasks_by_id;
    rwspin<std::unordered_map<std::coroutine_handle<>, std::shared_ptr<task<>>>> tasks_by_handle;
    rwspin<std::unordered_map<std::coroutine_handle<>, task_id>> task_ids_by_handle;

    atomic_size_t worker_count{0};
    std::vector<worker *> workers;
    size_t calcu_worker_count{0};
    atomic_size_t calcu_worker_load{0};
    size_t io_worker_count{0};
    atomic_size_t io_worker_load{0};

    task_sender io_task_tx;
    awake_queue io_worker_queue;
    task_sender calcu_task_tx;
    awake_queue calcu_worker_queue;

    atomic_size_t task_id_generator{1};

    static runtime *_this_runtime;
};

inline runtime *runtime::_this_runtime{nullptr};

};  // namespace asco::core
