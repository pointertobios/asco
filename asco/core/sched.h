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

#include <asco/coro_local.h>
#include <asco/perf.h>
#include <asco/rterror.h>
#include <asco/sync/spin.h>

namespace asco::core::sched {

using base::__coro_local_frame;

struct task {
    using task_id = size_t;

    task_id id;
    std::coroutine_handle<> handle;

    __coro_local_frame *coro_local_frame;

    // A sort of task that blocking worker thread and don't be stolen to other workers.
    bool is_blocking;
    bool is_inline{false};

#ifdef ASCO_PERF_RECORD
    perf::coro_recorder *perf_recorder{nullptr};
#endif

    // Use for timers. After related timer clocked, coroutine must start to run as fast as possible.
    // Set this to true to let this worker schedule this task earlier or let other worker thread steal this
    // task earlier.
    bool real_time{false};

    bool aborted{false};
    task_id waiting{0};

    bool mutable destroyed{false};

    __asco_always_inline void coro_frame_exit() {
        coro_local_frame->subframe_exit();
        if (!coro_local_frame->get_ref_count())
            delete coro_local_frame;
    }

    __asco_always_inline bool operator==(task &rhs) const { return id == rhs.id; }

    __asco_always_inline void resume() const {
        if (handle.done())
            throw asco::runtime_error("[ASCO] task::resume() Inner error: task is done but not destroyed.");
        handle.resume();
    }

    __asco_always_inline bool done() const {
        if (destroyed)
            return true;
        bool b = handle.done();
        return b;
    }

    __asco_always_inline void destroy() {
        if (!destroyed) {
#ifdef ASCO_PERF_RECORD
            if (perf_recorder)
                delete perf_recorder;
#endif
            coro_frame_exit();
            handle.destroy();
            destroyed = true;
        }
    }

    __asco_always_inline void set_real_time() { real_time = true; }

    __asco_always_inline void reset_real_time() {
        if (real_time)
            real_time = false;
    }
};

class std_scheduler {
public:
    using task = task;

    struct task_control {
        task t;
        enum class state {
            ready,
            running,
            suspending,
        } s{state::running};
    };

    void push_task(task t, task_control::state initial_state);
    std::optional<task *> sched();
    void try_reawake_buffered();
    std::optional<std::tuple<task_control *, std::binary_semaphore *>> steal(task::task_id id);
    void steal_from(task_control *, std::binary_semaphore *);
    bool currently_finished_all();
    bool has_buffered_awakes();

    void awake(task::task_id id);
    void suspend(task::task_id id);
    void destroy(task::task_id id, bool no_sync_awake = false);

    bool task_exists(task::task_id id);
    task &get_task(task::task_id id);
    task_control::state get_state(task::task_id id);

    void register_sync_awaiter(task::task_id id);
    std::binary_semaphore &get_sync_awaiter(task::task_id id);
    std::binary_semaphore *clone_sync_awaiter(task::task_id id);
    void emplace_sync_awaiter(task::task_id id, std::binary_semaphore *sem);

private:
    spin<std::vector<task_control *>> active_tasks;
    spin<std::unordered_map<task::task_id, task_control *>> suspended_tasks;

    std::set<task::task_id> not_in_suspended_but_awake_tasks;
    std::set<task::task_id> stealed_but_active_tasks;

    std::unordered_map<task::task_id, task_control *> task_map;
    std::unordered_map<task::task_id, std::binary_semaphore *> sync_awaiters;
};

};  // namespace asco::core::sched

#endif
