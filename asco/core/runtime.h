// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_RUNTIME_H
#define ASCO_RUNTIME_H

#include <coroutine>
#include <functional>
#include <map>
#include <optional>
#include <semaphore>
#include <stack>
#include <thread>
#include <unordered_map>
#include <vector>

#include <asco/core/io_thread.h>
#include <asco/core/linux/io_uring.h>
#include <asco/core/sched.h>
#include <asco/core/taskgroup.h>
#include <asco/core/timer.h>
#include <asco/rterror.h>
#include <asco/sync/rwspin.h>
#include <asco/sync/spin.h>
#include <asco/utils/channel.h>
#include <asco/utils/concepts.h>

namespace asco::core {

using namespace types;

using base::__coro_local_frame;

class worker;
using worker_fn = std::function<void(worker *)>;

using task_instance = std::coroutine_handle<>;
using task_sender = inner::ms::sender<sched::task>;
using task_receiver = inner::ms::receiver<sched::task>;

class worker {
public:
    using scheduler = sched::std_scheduler;
    using task_id = sched::task::task_id;

    explicit worker(size_t id, const worker_fn &f, task_receiver rx);
    worker(const worker &) = delete;
    worker(worker &) = delete;
    worker(worker &&) = delete;

    std::thread::id get_thread_id() const;
    void conditional_suspend();
    void awake();

    static void remove_task_map(task_id id);
    static void insert_task_map(task_id id, worker *self);
    static void modify_task_map(task_id id, worker *self);

    __asco_always_inline sched::task &current_task() { return *running_task.top(); }

    __asco_always_inline task_id current_task_id() { return running_task.top()->id; }

    __asco_always_inline bool task_schedulable(task_id id) { return sc.task_exists(id); }

#ifdef ASCO_IO_URING
    __asco_always_inline _linux::uring &get_uring() { return io_uring; }
#endif

    int id;
    bool is_calculator;
    task_receiver task_rx;
    scheduler sc;

#ifdef __linux__
    int pid{0};
#endif

    std::stack<sched::task *> running_task;

    std::binary_semaphore worker_await_sem{0};

private:
    std::jthread thread;

#ifdef ASCO_IO_URING
    _linux::uring io_uring;
#endif

public:
    static bool in_worker();
    static worker &get_worker();
    static bool task_available(task_id id);
    static worker &get_worker_from_task_id(task_id id);
    static void set_task_sem(task_id id);
    static rwspin<std::map<task_id, std::binary_semaphore *>> workers_by_task_id_sem;
    static std::map<task_id, worker *> workers_by_task_id;

private:
    static std::unordered_map<std::thread::id, worker *> workers;
    thread_local static worker *current_worker;
};

class runtime {
public:
    struct sys {
        static void set_args(int argc, const char **argv);
        __asco_always_inline static std::vector<std::string> &args() { return __args; }

        static void set_env(const char **env);
        __asco_always_inline static std::unordered_map<std::string, std::string> &env() { return __env; }

    private:
        static std::vector<std::string> __args;
        static std::unordered_map<std::string, std::string> __env;
    };

public:
    using __worker = worker;
    using scheduler = worker::scheduler;

    using task_id = sched::task::task_id;

    explicit runtime(size_t nthread = 0);
    explicit runtime(const runtime &) = delete;
    explicit runtime(const runtime &&) = delete;
    ~runtime();

    void send_task(sched::task task);
    void send_blocking_task(sched::task task);
    task_id spawn(task_instance task, __coro_local_frame *pframe, unwind::coro_trace trace, task_id gid = 0);
    task_id
    spawn_blocking(task_instance task, __coro_local_frame *pframe, unwind::coro_trace trace, task_id gid = 0);
    void awake(task_id id);
    void suspend(task_id id);
    void abort(task_id id);

    void register_sync_awaiter(task_id id);
    task_id task_id_from_corohandle(std::coroutine_handle<> handle);
    sched::task
    to_task(task_instance task, bool is_blocking, __coro_local_frame *pframe, unwind::coro_trace trace);

    void remove_task_map(void *addr);

    __asco_always_inline void inc_io_load() { io_worker_load++; }
    __asco_always_inline void dec_io_load() { io_worker_load--; }
    __asco_always_inline void inc_calcu_load() { calcu_worker_load++; }
    __asco_always_inline void dec_calcu_load() { calcu_worker_load--; }

    void timer_attach(task_id id, std::chrono::high_resolution_clock::time_point time);
    void timer_detach(task_id id);

    void join_task_to_group(task_id id, task_id gid, bool orogin = false);
    // Exit group and if the group has only 1 task, destroy that group.
    void exit_group(task_id id);
    bool in_group(task_id id);
    task_group *group(task_id id);

private:
    size_t nthread;
    std::vector<worker *> pool;

    spin<std::unordered_map<task_id, task_group *>> task_groups;

    std::optional<task_sender> io_task_tx{std::nullopt};
    atomic_size_t io_worker_count{0};
    atomic_size_t io_worker_load{0};
    std::optional<task_sender> calcu_task_tx{std::nullopt};
    atomic_size_t calcu_worker_count{0};
    atomic_size_t calcu_worker_load{0};

    // <pointer of coroutine handle, task_id>
    spin<std::unordered_map<void *, task_id>> coro_to_task_id;
    atomic<task_id> task_counter{1};

    timer::timer timer;
#ifndef ASCO_IO_URING
    io_thread io;
#endif

    void awake_all();

public:
    __asco_always_inline static runtime &get_runtime() {
        if (!current_runtime)
            throw asco::runtime_error(
                "[ASCO] runtime::get_runtime(): The async function must be called with asco::runtime initialized");
        return *current_runtime;
    }

    __asco_always_inline worker &get_worker_from_id(size_t wid) {
        if (pool.size() < wid)
            throw asco::runtime_error(
                std::format("[ASCO] runtime::get_worker_from_id(): Worker {} not initialized.", wid));
        return *pool[wid];
    }

private:
    static runtime *current_runtime;
};

};  // namespace asco::core

using RT = asco::core::runtime;

#endif
