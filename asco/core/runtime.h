// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_RUNTIME_H
#define ASCO_RUNTIME_H

#include <atomic>
#include <condition_variable>
#include <coroutine>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <semaphore>
#include <stack>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <asco/core/sched.h>
#include <asco/core/sync_awaiter.h>
#include <asco/utils/channel.h>
#include <asco/utils/concepts.h>

namespace asco {

class worker;
using worker_fn = std::function<void(worker *)>;

using task_instance = std::coroutine_handle<>;
using task_sender = inner::sender<sched::task>;
using task_receiver = inner::shared_receiver<sched::task>;

class worker {
public:
    using scheduler = sched::std_scheduler;
    using task_id = sched::task::task_id;

    explicit worker(int id, const worker_fn &f, task_receiver rx);
    worker(const worker &) = delete;
    worker(worker &) = delete;
    worker(worker &&) = delete;
    ~worker();

    void join();
    std::thread::id get_thread_id() const;
    void conditional_suspend();
    void awake();

    static void remove_task_map(task_id id);
    static void insert_task_map(task_id id, worker *self);

    __always_inline sched::task &current_task() {
        auto id = running_task.top().id;
        return sc.get_task(id);
    }

    __always_inline task_id current_task_id() {
        return running_task.top().id;
    }

    static bool in_worker();
    static worker *get_worker();

    int id;
    bool is_calculator;
    task_receiver task_rx;
    scheduler sc;

#ifdef __linux__
    int pid{0};
#endif

    std::stack<sched::task> running_task;

    std::binary_semaphore worker_await_sem{0};

    std::unordered_map<task_id, inner::sender<__u8>> sync_awaiters_tx;

private:
    std::jthread thread;

    bool moved{false};

public:
    static worker *get_worker_from_task_id(task_id id);
    static void set_task_sem(task_id id);
    static std::map<task_id, std::binary_semaphore *> workers_by_task_id_sem;
    static std::mutex wsem_mutex;
    static std::map<task_id, worker *> workers_by_task_id;

private:
    static std::unordered_map<std::thread::id, worker *> workers;
    thread_local static worker *current_worker;
};

template<typename T>
concept is_runtime = requires(T t) {
    typename T::__worker;
    typename T::scheduler;
    typename T::scheduler::task;
    typename T::task_id;
    typename T::sys;
    // Exception: runtime error when there is not a worker on the current thread.
    { T::__worker::get_worker() } -> std::same_as<typename T::__worker *>;
    { T::__worker::set_task_sem(typename T::task_id{}) } -> std::same_as<void>;
    { T::__worker::remove_task_map(typename T::task_id{}) } -> std::same_as<void>;
    { T::__worker::insert_task_map(typename T::task_id{}, std::declval<typename T::__worker *>()) } -> std::same_as<void>;
    { T::get_runtime() } -> std::same_as<T *>;
    { t.spawn(task_instance{}, std::declval<__coro_local_frame *>()) } -> std::same_as<typename T::task_id>;
    { t.spawn_blocking(task_instance{}, std::declval<__coro_local_frame *>()) } -> std::same_as<typename T::task_id>;
    { t.awake(typename T::task_id{}) } -> std::same_as<void>;
    { t.suspend(typename T::task_id{}) } -> std::same_as<void>;
    { t.register_sync_awaiter(typename T::task_id{}) } -> std::same_as<sync_awaiter>;
    { t.task_id_from_corohandle(std::declval<std::coroutine_handle<>>()) } -> std::same_as<typename T::task_id>;
    { t.to_task(std::declval<task_instance>(), bool{}, std::declval<__coro_local_frame *>()) } -> std::same_as<sched::task>;
    { t.remove_task_map(nullptr) } -> std::same_as<void>;
} && requires(T::__worker w) {
    { w.conditional_suspend() } -> std::same_as<void>;
    { w.current_task() } -> std::same_as<typename T::scheduler::task &>;
    { w.current_task_id() } -> std::same_as<typename T::task_id>;
    { w.is_calculator } -> std::same_as<bool &>;
    { w.running_task } -> std::same_as<std::stack<typename T::scheduler::task> &>;
} && sched::is_scheduler<typename T::scheduler>;

class runtime {
public:
    struct sys {
        static void set_args(int argc, const char **argv);
        __always_inline
        static std::vector<std::string> &args() {
            return __args;
        }

        static void set_env(char **env);
        __always_inline
        static std::unordered_map<std::string, std::string> &env() {
            return __env;
        }
    private:
        static std::vector<std::string> __args;
        static std::unordered_map<std::string, std::string> __env;
    };

public:
    using __worker = worker;
    using scheduler = worker::scheduler;

    using task_id = sched::task::task_id;

    explicit runtime(int nthread = 0);
    explicit runtime(const runtime&) = delete;
    explicit runtime(const runtime&&) = delete;
    ~runtime();

    task_id spawn(task_instance task, __coro_local_frame *pframe);
    task_id spawn_blocking(task_instance task, __coro_local_frame *pframe);
    void awake(task_id id);
    void suspend(task_id id);

    sync_awaiter register_sync_awaiter(task_id id);
    task_id task_id_from_corohandle(std::coroutine_handle<> handle);
    sched::task to_task(task_instance task, bool is_blocking, __coro_local_frame *pframe);

    void remove_task_map(void *addr);

private:
    int nthread;
    std::vector<worker *> pool;

    std::optional<task_sender> io_task_tx{std::nullopt};
    std::atomic<int> io_worker_count{0};
    int io_worker_load{0};
    std::optional<task_sender> calcu_task_tx{std::nullopt};
    std::atomic<int> calcu_worker_count{0};
    int calcu_worker_load{0};

    std::map<void *, task_id> coro_to_task_id;
    std::mutex coro_to_task_id_mutex;
    atomic<task_id> task_counter{1};

    void awake_all();

public:
    __always_inline static runtime *get_runtime() {
        if (!current_runtime)
            throw std::runtime_error("[ASCO] runtime::get_runtime(): The async function must be called with asco::runtime initialized");
        return current_runtime;
    }

private:
    static runtime *current_runtime;
};

// Define macro `SET_RUNTIME` with set_runtime(rt)
// And use this before include future.h
#define set_runtime(rt) \
    using RT = rt

}; // namespace asco

#endif
