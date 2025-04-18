// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_SCHED_H
#define ASCO_SCHED_H

#include <atomic>
#include <chrono>
#include <concepts>
#include <mutex>
#include <coroutine>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

#include <asco/coro_local.h>

namespace asco::sched {

struct task {
    using task_id = size_t;

    task_id id;
    std::coroutine_handle<> handle;

    bool is_blocking;
    bool is_inline{false};
    bool mutable destroyed{false};

    __coro_local_frame *coro_local_frame{new __coro_local_frame};

    __always_inline bool operator==(task &rhs) const {
        return id == rhs.id;
    }

    __always_inline void resume() const {
        if (handle.done())
            throw std::runtime_error("[ASCO] Inner error: task is done but not destroyed.");
        handle.resume();
    }

    __always_inline bool done() const {
        bool b = handle.done();
        if (b && !destroyed) {
            handle.destroy();
            destroyed = true;
        }
        return b;
    }
};

template<typename T>
concept is_scheduler = requires(T t) {
    typename T::task;
    typename T::task_control;
    { t.push_task(task{}, std::declval<typename T::task_control::__control_state>()) } -> std::same_as<void>;
    { t.sched() } -> std::same_as<std::optional<task>>;
    { t.try_reawake_buffered() } -> std::same_as<void>;
    { t.find_stealable_and_steal() } -> std::same_as<std::optional<task>>;
    { t.steal(task::task_id{}) } -> std::same_as<std::optional<task>>;
    { t.currently_finished_all() } -> std::same_as<bool>;
    { t.has_buffered_awakes() } -> std::same_as<bool>;
    { t.awake(task::task_id{}) } -> std::same_as<void>;
    { t.suspend(task::task_id{}) } -> std::same_as<void>;
    { t.destroy(task::task_id{}) } -> std::same_as<void>;
    { t.task_exists(task::task_id{}) } -> std::same_as<bool>;
    { t.get_task(task::task_id{}) } -> std::same_as<task &>;
};

// I call it std_scheduler because it uses STL.
class std_scheduler {
public:
    using task = task;

    struct task_control {
        task t;
        enum class __control_state {
            running,
            suspending,
        } state{__control_state::running};
    };

    std_scheduler();
    void push_task(task t, task_control::__control_state initial_state);
    std::optional<task> sched();
    void try_reawake_buffered();
    std::optional<task> find_stealable_and_steal();
    std::optional<task> steal(task::task_id id);
    bool currently_finished_all();
    bool has_buffered_awakes();

    void awake(task::task_id id);
    void suspend(task::task_id id);
    void destroy(task::task_id id);

    bool task_exists(task::task_id id);
    task &get_task(task::task_id id);

private:
    std::vector<task_control *> active_tasks;
    std::mutex active_tasks_mutex;
    std::unordered_map<task::task_id, task_control *> suspended_tasks;

    std::set<task::task_id> not_in_suspended_but_awake_tasks;

    std::unordered_map<task::task_id, task> task_map;
};

};

#endif
