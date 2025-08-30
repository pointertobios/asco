// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_SCHED_H
#define ASCO_SCHED_H

#include <coroutine>
#include <optional>
#include <semaphore>
#include <set>
#include <unordered_map>
#include <vector>

#include <asco/core/slub.h>
#include <asco/coro_local.h>
#include <asco/coroutine_allocator.h>
#include <asco/perf.h>
#include <asco/rterror.h>
#include <asco/sync/spin.h>
#include <asco/utils/pubusing.h>

namespace asco::core::sched {

using namespace types;

using base::__coro_local_frame;

struct task {
    using task_id = size_t;

    task_id id;
    std::coroutine_handle<> handle;

    __coro_local_frame *coro_local_frame;

#ifdef ASCO_PERF_RECORD
    perf::coro_recorder *perf_recorder{nullptr};
#endif

    std::atomic<task_id> waiting{0};

    enum class state {
        ready,
        running,
        suspending,
    } st{state::running};

    // A sort of task that blocking worker thread and don't be stolen to other workers.
    bool is_blocking;
    bool is_inline{false};

    // Use for timers. After related timer clocked, coroutine must start to run as fast as possible.
    // Set this to true to let this worker schedule this task earlier or let other worker thread steal
    // this task earlier.
    bool real_time{false};

    atomic_bool aborted{false};

    bool mutable destroyed{false};

    task(
        task_id id, std::coroutine_handle<> handle, __coro_local_frame *coro_local_frame, bool is_blocking,
        bool is_inline = false);

    bool operator==(const task &rhs) const;
    void resume() const;
    bool done() const;
    void destroy();
    void free_only();
    void coro_frame_exit();

    __asco_always_inline void set_real_time() { real_time = true; }

    __asco_always_inline void reset_real_time() {
        if (real_time)
            real_time = false;
    }

    void *operator new(std::size_t) noexcept { return slub_cache.allocate(); }
    void operator delete(void *p) noexcept { slub_cache.deallocate(static_cast<task *>(p)); }

private:
    static slub::cache<task> slub_cache;
};

inline slub::cache<task> task::slub_cache{};

class std_scheduler {
public:
    void push_task(task *t, task::state initial_state);
    task *sched();
    void try_reawake_buffered();
    std::optional<std::tuple<task *, std::binary_semaphore *>> steal(task::task_id id);
    void steal_from(task *, std::binary_semaphore *);
    bool currently_finished_all();
    bool has_buffered_awakes();

    void awake(task::task_id id);
    void suspend(task::task_id id);
    void destroy(task::task_id id, bool no_sync_awake = false);
    void free_only(task::task_id id, bool no_sync_awake = false);

    bool task_exists(task::task_id id);
    task *get_task(task::task_id id);
    task::state get_state(task::task_id id);

    void register_sync_awaiter(task::task_id id);
    std::binary_semaphore &get_sync_awaiter(task::task_id id);
    std::binary_semaphore *clone_sync_awaiter(task::task_id id);
    void emplace_sync_awaiter(task::task_id id, std::binary_semaphore *sem);

private:
    spin<std::vector<task *>> active_tasks;
    spin<std::unordered_map<task::task_id, task *>> suspended_tasks;

    std::set<task::task_id> not_in_suspended_but_awake_tasks;
    std::set<task::task_id> stealed_but_active_tasks;

    std::unordered_map<task::task_id, task *> task_map;
    std::unordered_map<task::task_id, std::binary_semaphore *> sync_awaiters;
};

};  // namespace asco::core::sched

#endif
