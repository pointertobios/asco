// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <asco/core/sched.h>

namespace asco::core::sched {

void std_scheduler::push_task(task t, task_control::__control_state initial_state) {
    auto guard = active_tasks.lock();
    auto p = new task_control{t};
    task_map.emplace(t.id, p);
    p->state = initial_state;
    if (initial_state == task_control::__control_state::running)
        guard->push_back(p);
    else
        suspended_tasks.lock()->emplace(t.id, p);
}

std::optional<task *> std_scheduler::sched() {
    auto guard = active_tasks.lock();
    if (guard->empty()) {
        return std::nullopt;
    }
    while (!guard->empty()) {
        auto task = *guard->begin();
        guard->erase(guard->begin());
        if (task->state == task_control::__control_state::suspending) {
            (*suspended_tasks.lock())[task->t.id] = task;
            continue;
        }
        guard->push_back(task);
        return &task->t;
    }
    return std::nullopt;
}

void std_scheduler::try_reawake_buffered() {
    for (auto id : not_in_suspended_but_awake_tasks) {
        auto guard = active_tasks.lock();
        auto susg = suspended_tasks.lock();
        if (auto it = susg->find(id); it != susg->end()) {
            it->second->state = task_control::__control_state::running;
            guard->push_back(it->second);
            susg->erase(it);
            not_in_suspended_but_awake_tasks.erase(id);
            return;
        }
    }
}

bool std_scheduler::currently_finished_all() {
    auto guard = suspended_tasks.lock();
    std::erase_if(*guard, [](auto &p) { return p.second->t.done(); });
    return active_tasks.lock()->empty() && guard->empty();
}

bool std_scheduler::has_buffered_awakes() { return !not_in_suspended_but_awake_tasks.empty(); }

void std_scheduler::awake(task::task_id id) {
#ifdef ASCO_PERF_RECORD
    get_task(id).perf_recorder->record_once();
#endif

    auto guard = active_tasks.lock();
    auto susg = suspended_tasks.lock();
    if (auto it = susg->find(id); it != susg->end()) {
        it->second->state = task_control::__control_state::running;
        guard->push_back(it->second);
        susg->erase(it);
    } else {
        not_in_suspended_but_awake_tasks.insert(id);
    }
}

void std_scheduler::suspend(task::task_id id) {
#ifdef ASCO_PERF_RECORD
    get_task(id).perf_recorder->record_once();
#endif

    if (not_in_suspended_but_awake_tasks.find(id) != not_in_suspended_but_awake_tasks.end()) {
        not_in_suspended_but_awake_tasks.erase(id);
        return;
    }

    auto guard = active_tasks.lock();
    if (auto it = std::find_if(guard->begin(), guard->end(), [id](task_control *t) { return t->t.id == id; });
        it != guard->end()) {
        (*it)->state = task_control::__control_state::suspending;
        (*suspended_tasks.lock())[id] = *it;
        guard->erase(it);
    }
}

void std_scheduler::destroy(task::task_id id, bool no_sync_awake) {
    if (!no_sync_awake) {
        if (auto it = sync_awaiters.find(id); it != sync_awaiters.end())
            it->second->release();
    }

    auto guard = active_tasks.lock();
    task_map.erase(id);
    if (auto it = std::find_if(guard->begin(), guard->end(), [id](task_control *t) { return t->t.id == id; });
        it != guard->end()) {
        (*it)->t.destroy();
        delete *it;

        guard->erase(it);
        return;
    }

    auto susg = suspended_tasks.lock();
    auto iter = susg->find(id);
    if (iter == susg->end())
        return;

    iter->second->t.destroy();
    delete iter->second;

    susg->erase(iter);
}

bool std_scheduler::task_exists(task::task_id id) {
    auto guard = active_tasks.lock();
    auto susg = suspended_tasks.lock();
    return std::find_if(guard->begin(), guard->end(), [id](task_control *t) { return t->t.id == id; })
               != guard->end()
           || susg->find(id) != susg->end();
}

task &std_scheduler::get_task(task::task_id id) {
    if (auto it = task_map.find(id); it != task_map.end()) {
        return it->second->t;
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

std::binary_semaphore *std_scheduler::clone_sync_awaiter(task::task_id id) {
    if (auto it = sync_awaiters.find(id); it != sync_awaiters.end()) {
        return it->second;
    }
    return nullptr;
}

void std_scheduler::emplace_sync_awaiter(task::task_id id, std::binary_semaphore *sem) {
    sync_awaiters.emplace(id, sem);
}

};  // namespace asco::core::sched
