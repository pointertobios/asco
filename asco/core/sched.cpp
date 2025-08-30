// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/sched.h>
#include <asco/exception.h>

namespace asco::core::sched {

task::task(
    task_id id, std::coroutine_handle<> handle, __coro_local_frame *coro_local_frame, bool is_blocking,
    bool is_inline)
        : id(id)
        , handle(handle)
        , coro_local_frame(coro_local_frame)
        , is_blocking(is_blocking)
        , is_inline(is_inline) {}

void std_scheduler::push_task(task *t, task::state initial_state) {
    task_map.emplace(t->id, t);
    t->st = initial_state;
    if (initial_state == task::state::running)
        active_tasks.lock()->push_back(t);
    else
        suspended_tasks.lock()->emplace(t->id, t);
}

bool task::operator==(const task &rhs) const { return id == rhs.id; }

void task::resume() const {
    if (handle.done())
        throw asco::runtime_error("[ASCO] task::resume() Inner error: task is done but not destroyed.");
    handle.resume();
}

bool task::done() const {
    if (destroyed)
        return true;
    bool b = handle.done();
    return b;
}

void task::destroy() {
    if (destroyed)
        return;

#ifdef ASCO_PERF_RECORD
    if (perf_recorder)
        delete perf_recorder;
#endif
    coro_frame_exit();
    handle.destroy();
    destroyed = true;
}

void task::free_only() {
    if (destroyed)
        return;

    coro_frame_exit();
    base::coroutine_allocator::deallocate(handle.address());
    destroyed = true;
}

void task::coro_frame_exit() {
    coro_local_frame->subframe_exit();
    if (!coro_local_frame->get_ref_count())
        delete coro_local_frame;
}

task *std_scheduler::sched() {
    auto guard = active_tasks.lock();
    if (guard->empty()) {
        return nullptr;
    }
    while (!guard->empty()) {
        auto task = *guard->begin();
        guard->erase(guard->begin());

        if (stealed_but_active_tasks.contains(task->id))
            continue;

        if (task->st == task::state::suspending) {
            (*suspended_tasks.lock())[task->id] = task;
            continue;
        }

        guard->push_back(task);
        return task;
    }
    return nullptr;
}

void std_scheduler::try_reawake_buffered() {
    for (auto id : not_in_suspended_but_awake_tasks) {
        auto guard = active_tasks.lock();
        auto susg = suspended_tasks.lock();
        if (auto it = susg->find(id); it != susg->end()) {
            it->second->st = task::state::running;
            guard->push_back(it->second);
            susg->erase(it);
            not_in_suspended_but_awake_tasks.erase(id);
            return;
        }
    }
}

std::optional<std::tuple<task *, std::binary_semaphore *>> std_scheduler::steal(task::task_id id) {
    if (!task_map.contains(id))
        return std::nullopt;

    auto *task = task_map[id];
    task_map.erase(id);
    std::binary_semaphore *awaiter{nullptr};
    if (auto it = sync_awaiters.find(id); it != sync_awaiters.end()) {
        awaiter = it->second;
        sync_awaiters.erase(it);
    }

    if (auto guard = suspended_tasks.lock(); guard->contains(id))
        guard->erase(id);
    else
        stealed_but_active_tasks.insert(id);

    return std::make_tuple(task, awaiter);
}

void std_scheduler::steal_from(task *task, std::binary_semaphore *awaiter) {
    task_map[task->id] = task;
    if (awaiter)
        sync_awaiters[task->id] = awaiter;

    if (task->st == task::state::running)
        active_tasks.lock()->push_back(task);
    else
        suspended_tasks.lock()->emplace(task->id, task);
}

bool std_scheduler::currently_finished_all() {
    auto guard = suspended_tasks.lock();
    std::erase_if(*guard, [](auto &p) { return p.second->done(); });
    return active_tasks.lock()->empty() && guard->empty();
}

bool std_scheduler::has_buffered_awakes() { return !not_in_suspended_but_awake_tasks.empty(); }

void std_scheduler::awake(task::task_id id) {
    auto guard = active_tasks.lock();
    auto susg = suspended_tasks.lock();
    if (auto it = susg->find(id); it != susg->end()) {
        it->second->st = task::state::running;
        guard->push_back(it->second);
        susg->erase(it);
    } else {
        not_in_suspended_but_awake_tasks.insert(id);
    }
}

void std_scheduler::suspend(task::task_id id) {
    if (not_in_suspended_but_awake_tasks.find(id) != not_in_suspended_but_awake_tasks.end()) {
        not_in_suspended_but_awake_tasks.erase(id);
        return;
    }

    auto guard = active_tasks.lock();
    if (auto it = std::find_if(guard->begin(), guard->end(), [id](task *t) { return t->id == id; });
        it != guard->end()) {
        (*it)->st = task::state::suspending;
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
    if (auto it = std::find_if(guard->begin(), guard->end(), [id](task *t) { return t->id == id; });
        it != guard->end()) {
        (*it)->destroy();
        delete *it;

        guard->erase(it);
        return;
    }

    auto susg = suspended_tasks.lock();
    auto iter = susg->find(id);
    if (iter == susg->end())
        return;

    iter->second->destroy();
    delete iter->second;

    susg->erase(iter);
}

void std_scheduler::free_only(task::task_id id, bool no_sync_awake) {
    if (!no_sync_awake) {
        if (auto it = sync_awaiters.find(id); it != sync_awaiters.end())
            it->second->release();
    }

    auto guard = active_tasks.lock();
    task_map.erase(id);
    if (auto it = std::find_if(guard->begin(), guard->end(), [id](task *t) { return t->id == id; });
        it != guard->end()) {
        (*it)->free_only();
        delete *it;

        guard->erase(it);
        return;
    }

    auto susg = suspended_tasks.lock();
    auto iter = susg->find(id);
    if (iter == susg->end())
        return;

    iter->second->free_only();
    delete iter->second;

    susg->erase(iter);
}

bool std_scheduler::task_exists(task::task_id id) {
    auto guard = active_tasks.lock();
    auto susg = suspended_tasks.lock();
    return std::find_if(guard->begin(), guard->end(), [id](task *t) { return t->id == id; }) != guard->end()
           || susg->find(id) != susg->end();
}

task *std_scheduler::get_task(task::task_id id) {
    if (auto it = task_map.find(id); it != task_map.end()) {
        return it->second;
    } else {
        throw asco::inner_exception(
            std::format(
                "[ASCO] std_scheduler::get_task(): Task {} not found (maybe because you call this function in synchronous texture)",
                id));
    }
}

task::state std_scheduler::get_state(task::task_id id) {
    if (auto it = task_map.find(id); it != task_map.end()) {
        return it->second->st;
    } else {
        throw asco::inner_exception(std::format("[ASCO] std_scheduler::get_state(): Task {} not found", id));
    }
}

void std_scheduler::register_sync_awaiter(task::task_id id) {
    sync_awaiters.emplace(id, new std::binary_semaphore{0});
}

std::binary_semaphore &std_scheduler::get_sync_awaiter(task::task_id id) {
    if (auto it = sync_awaiters.find(id); it != sync_awaiters.end()) {
        return *it->second;
    } else {
        throw asco::inner_exception(
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
