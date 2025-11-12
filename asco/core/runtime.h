// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <coroutine>
#include <memory>
#include <unordered_map>
#include <vector>

#include <asco/core/time/timer_concept.h>
#include <asco/core/worker.h>
#include <asco/sync/rwspin.h>
#include <asco/utils/defines.h>
#include <asco/utils/types.h>

namespace asco::core {

using namespace asco::types;

struct runtime_builder {
    std::unique_ptr<time::timer_concept> timer{
        std::make_unique<time::timer_concept>(std::make_unique<time::high_resolution_timer>())};
    size_t parallel{0};
};

class runtime {
public:
    static runtime &init(runtime_builder &&builder) noexcept;
    static runtime &this_runtime() noexcept;

    time::timer_concept &timer() noexcept;

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
    explicit runtime(size_t parallel, std::unique_ptr<time::timer_concept> &&timer_ptr);
    ~runtime();

    rwspin<std::unordered_map<task_id, std::shared_ptr<task<>>>> tasks_by_id;
    rwspin<std::unordered_map<std::coroutine_handle<>, std::shared_ptr<task<>>>> tasks_by_handle;
    rwspin<std::unordered_map<std::coroutine_handle<>, task_id>> task_ids_by_handle;

    atomic_size_t worker_count{0};
    atomic_bool shutting_down{false};
    std::vector<std::unique_ptr<worker>> workers;
    size_t calcu_worker_count{0};
    atomic_size_t calcu_worker_load{0};
    size_t io_worker_count{0};
    atomic_size_t io_worker_load{0};

    task_sender io_task_tx;
    awake_queue io_worker_queue;
    task_sender calcu_task_tx;
    awake_queue calcu_worker_queue;

    atomic_size_t task_id_generator{1};

    std::unique_ptr<time::timer_concept> _timer;

    static runtime *_this_runtime;
};

inline runtime *runtime::_this_runtime{nullptr};

};  // namespace asco::core
