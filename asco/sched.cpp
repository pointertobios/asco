// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <asco/sched.h>

#include <iostream>

namespace asco::sched {

    std_scheduler::std_scheduler() {}

    void std_scheduler::push_task(task t) {
        std::lock_guard lk{active_tasks_mutex};
        auto p = new task_control{t};
        p->state = task_control::__control_state::running;
        active_tasks.push_back(p);
    }

    std::optional<task> std_scheduler::sched() {
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
            return task->t;
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
        return active_tasks.empty() && suspended_tasks.empty();
    }

    bool std_scheduler::has_buffered_awakes() {
        return !not_in_suspended_but_awake_tasks.empty();
    }

    void std_scheduler::awake(task::task_id id) {
        // std::cout << std::format("scheduler: awake task {}\n", id);
        std::lock_guard lk{active_tasks_mutex};
        if (auto it = suspended_tasks.find(id); it != suspended_tasks.end()) {
            it->second->state = task_control::__control_state::running;
            active_tasks.push_back(it->second);
            suspended_tasks.erase(it);
        } else {
            // std::cout << std::format("scheduler: task {} not in suspended tasks\n", id);
            not_in_suspended_but_awake_tasks.insert(id);
        }
    }

    void std_scheduler::suspend(task::task_id id) {
        // std::cout << std::format("scheduler: suspend task {}\n", id);
        if (not_in_suspended_but_awake_tasks.find(id)
                != not_in_suspended_but_awake_tasks.end()) {
            // std::cout << std::format("scheduler: task {} awake right now\n", id);
            not_in_suspended_but_awake_tasks.erase(id);
            return;
        }
        std::lock_guard lk{active_tasks_mutex};
        for (auto &t : active_tasks) {
            if (t->t.id == id) {
                t->state = task_control::__control_state::suspending;
                break;
            }
        }
    }

    void std_scheduler::destroy(task::task_id id) {
        // std::cout << std::format("scheduler: destroying task {}\n", id);
        std::lock_guard lk{active_tasks_mutex};
        if (auto it = std::find_if(
                active_tasks.begin(), active_tasks.end(),
                [id] (task_control *t) { return t->t.id == id; });
            it != active_tasks.end()) {
            active_tasks.erase(it);
            return;
        }
        auto iter = suspended_tasks.find(id);
        if (iter == suspended_tasks.end())
            return;

        iter->second->t.done();
        delete iter->second;
        suspended_tasks.erase(iter);
    }

    bool std_scheduler::task_exists(task::task_id id) {
        std::lock_guard lk{active_tasks_mutex};
        return std::find_if(
                    active_tasks.begin(), active_tasks.end(),
                    [id] (task_control *t) { return t->t.id == id; })
                != active_tasks.end() ||
            suspended_tasks.find(id) != suspended_tasks.end();
    }

};
