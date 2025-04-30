// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <asco/core/sched.h>

namespace asco::sched {

void std_scheduler::push_task(task t, task_control::__control_state initial_state) {
    std::lock_guard lk{active_tasks_mutex};
    auto p = new task_control{t};
    task_map.emplace(t.id, p);
    p->state = initial_state;
    if (initial_state == task_control::__control_state::running)
        active_tasks.push_back(p);
    else
        suspended_tasks.emplace(t.id, p);
}

std::optional<task *> std_scheduler::sched() {
    if (active_tasks.empty()) {
        return std::nullopt;
    }
    std::lock_guard lk{active_tasks_mutex};
    while (!active_tasks.empty()) {
        auto task = *active_tasks.begin();
        active_tasks.erase(active_tasks.begin());
        if (task->state == task_control::__control_state::suspending) {
            suspended_tasks[task->t.id] = task;
            continue;
        }
        active_tasks.push_back(task);
        return &task->t;
    }
    return std::nullopt;
}

void std_scheduler::try_reawake_buffered() {
    for (auto id : not_in_suspended_but_awake_tasks) {
        std::lock_guard lk{active_tasks_mutex};
        if (auto it = suspended_tasks.find(id); it != suspended_tasks.end()) {
            it->second->state = task_control::__control_state::running;
            active_tasks.push_back(it->second);
            suspended_tasks.erase(it);
            not_in_suspended_but_awake_tasks.erase(id);
            return;
        }
    }
}

bool std_scheduler::currently_finished_all() {
    std::erase_if(suspended_tasks, [](auto &p) { return p.second->t.done(); });
    std::lock_guard lk{active_tasks_mutex};
    return active_tasks.empty() && suspended_tasks.empty();
}

bool std_scheduler::has_buffered_awakes() { return !not_in_suspended_but_awake_tasks.empty(); }

void std_scheduler::awake(task::task_id id) {
    std::lock_guard lk{active_tasks_mutex};
    if (auto it = suspended_tasks.find(id); it != suspended_tasks.end()) {
        it->second->state = task_control::__control_state::running;
        active_tasks.push_back(it->second);
        suspended_tasks.erase(it);
    } else {
        not_in_suspended_but_awake_tasks.insert(id);
    }
}

void std_scheduler::suspend(task::task_id id) {
    if (not_in_suspended_but_awake_tasks.find(id) != not_in_suspended_but_awake_tasks.end()) {
        not_in_suspended_but_awake_tasks.erase(id);
        return;
    }

    std::lock_guard lk{active_tasks_mutex};
    if (auto it = std::find_if(
            active_tasks.begin(), active_tasks.end(), [id](task_control *t) { return t->t.id == id; });
        it != active_tasks.end()) {
        (*it)->state = task_control::__control_state::suspending;
        suspended_tasks[id] = *it;
        active_tasks.erase(it);
    }
}

void std_scheduler::destroy(task::task_id id) {
    if (auto it = sync_awaiters.find(id); it != sync_awaiters.end())
        it->second->release();

    std::lock_guard lk{active_tasks_mutex};
    task_map.erase(id);
    if (auto it = std::find_if(
            active_tasks.begin(), active_tasks.end(), [id](task_control *t) { return t->t.id == id; });
        it != active_tasks.end()) {
        (*it)->t.destroy();
        delete *it;

        active_tasks.erase(it);
        return;
    }

    auto iter = suspended_tasks.find(id);
    if (iter == suspended_tasks.end())
        return;

    iter->second->t.destroy();
    delete iter->second;

    suspended_tasks.erase(iter);
}

bool std_scheduler::task_exists(task::task_id id) {
    std::lock_guard lk{active_tasks_mutex};
    return std::find_if(
               active_tasks.begin(), active_tasks.end(), [id](task_control *t) { return t->t.id == id; })
               != active_tasks.end()
           || suspended_tasks.find(id) != suspended_tasks.end();
}

task *std_scheduler::get_task(task::task_id id) {
    if (auto it = task_map.find(id); it != task_map.end()) {
        return &it->second->t;
    } else {
        throw std::runtime_error(
            std::format(
                "[ASCO] std_scheduler::get_task(): Task {} not found (maybe because you call this function in synchronous texture)",
                id));
    }
}

void std_scheduler::register_sync_awaiter(task::task_id id) {
    sync_awaiters.emplace(id, new std::binary_semaphore{0});
}

std::binary_semaphore &std_scheduler::get_sync_awaiter(task::task_id id) {
    if (auto it = sync_awaiters.find(id); it != sync_awaiters.end()) {
        return *it->second;
    } else {
        throw std::runtime_error(
            std::format("[ASCO] std_scheduler::get_sync_awaiter(): Sync awaiter of task {} not found", id));
    }
}

};  // namespace asco::sched
