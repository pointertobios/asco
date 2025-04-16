// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <asco/sched.h>

#include <iostream>

namespace asco::sched {

    std_scheduler::std_scheduler() {}

    void std_scheduler::push_task(task t, task_control::__control_state initial_state) {
        std::lock_guard lk{active_tasks_mutex};
        auto p = new task_control{t};
        p->state = initial_state;
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
        std::erase_if(suspended_tasks, [] (auto &p) { return p.second->t.done(); });
        return active_tasks.empty() && suspended_tasks.empty() && gave_outs.empty();
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
        // for (auto &t : active_tasks) {
        //     if (t->t.id == id) {
        //         t->state = task_control::__control_state::suspending;
        //         break;
        //     }
        // }
        if (auto it = std::find_if(
                active_tasks.begin(), active_tasks.end(),
                [id] (task_control *t) { return t->t.id == id; });
            it != active_tasks.end()) {
            (*it)->state = task_control::__control_state::suspending;
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
        delete iter->second->t.coro_local_frame;
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

    task &std_scheduler::get_task(task::task_id id) {
        auto it = std::find_if(
                    active_tasks.begin(), active_tasks.end(),
                    [id] (task_control *t) { return t->t.id == id; });
        if (it != active_tasks.end()) {
            return (*it)->t;
        } else if (auto it = suspended_tasks.find(id); it != suspended_tasks.end()) {
            return it->second->t;
        } else {
            throw std::runtime_error(std::format("[ASCO] Task {} not found (maybe because you call it in synchronous texture)", id));
        }
    }

    std_scheduler::task_handle std_scheduler::give_out(task::task_id id) {
        std::lock_guard lk{active_tasks_mutex};
        if (auto it = std::find_if(
                active_tasks.begin(), active_tasks.end(),
                [id] (task_control *t) { return t->t.id == id; });
            it != active_tasks.end()) {
            auto p = *it;
            active_tasks.erase(it);
            return std_scheduler::task_handle{p, this};
        }
        auto iter = suspended_tasks.find(id);
        if (iter == suspended_tasks.end())
            throw std::runtime_error(std::format("[ASCO] Task {} not found", id));
        
        auto p = iter->second;
        suspended_tasks.erase(iter);
        gave_outs.insert(id);
        return std_scheduler::task_handle{p, this};
    }

};
