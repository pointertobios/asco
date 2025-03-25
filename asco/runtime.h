#ifndef ASCO_RUNTIME_H
#define ASCO_RUNTIME_H

#include <atomic>
#include <condition_variable>
#include <coroutine>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <asco/sched.h>
#include <asco/sync_awaiter.h>
#include <asco/utils/channel.h>
#include <asco/utils/utils.h>

using namespace asco_inner;

namespace asco {

class worker;
using worker_fn = std::function<void(worker *)>;

using task_instance = std::coroutine_handle<>;
using task_sender = sender<sched::task>;
using task_receiver = shared_receiver<sched::task>;

class worker {
public:
    using scheduler = sched::std_scheduler;
    using task_id = sched::task::task_id;

    explicit worker(int id, const worker_fn &f, task_receiver rx) noexcept;
    worker(const worker &) = delete;
    worker(worker &) = delete;
    worker(worker &&) = delete;
    ~worker();

    void join();
    std::thread::id get_thread_id() const;
    void conditional_suspend();
    void awake();

    int id;
    int pid{0};
    bool is_calculator;
    task_receiver task_rx;
    scheduler sc;

    // worker thread states and cv
    std::atomic_bool suspending{false};
    bool awake_signal{false};
    std::condition_variable cv;
    std::mutex cv_mutex;

    std::unordered_map<task_id, asco_inner::sender<__u8>> sync_awaiters_tx;

private:
    std::jthread thread;

    bool moved{false};

public:
    static worker *get_worker_from_task_id(task_id id);
    static std::unordered_map<task_id, worker *> workers_by_task_id;

private:
    static std::unordered_map<std::thread::id, worker *> workers;
    static worker *get_worker();
    thread_local static worker *current_worker;
};

template<typename T>
concept is_runtime = requires(T t) {
    typename T::scheduler;
    typename T::task_id;
    { T::get_runtime() } -> std::same_as<T *>;
    { t.spawn(task_instance{}) } -> std::same_as<typename T::task_id>;
    { t.spawn_blocking(task_instance{}) } -> std::same_as<typename T::task_id>;
    { t.awake(typename T::task_id{}) } -> std::same_as<void>;
    { t.suspend(typename T::task_id{}) } -> std::same_as<void>;
    { t.register_sync_awaiter(typename T::task_id{}) } -> std::same_as<sync_awaiter>;
    { t.task_id_from_corohandle(std::declval<std::coroutine_handle<>>()) } -> std::same_as<typename T::task_id>;
} && sched::is_scheduler<typename T::scheduler>;

class runtime {
public:
    using scheduler = worker::scheduler;

    using task_id = sched::task::task_id;

    explicit runtime(int nthread = 0);
    explicit runtime(const runtime&) = delete;
    explicit runtime(const runtime&&) = delete;
    ~runtime();

    task_id spawn(task_instance task);
    task_id spawn_blocking(task_instance task);
    void awake(task_id id);
    void suspend(task_id id);

    sync_awaiter register_sync_awaiter(task_id id);
    task_id task_id_from_corohandle(std::coroutine_handle<> handle);

private:
    int nthread;
    std::vector<worker *> pool;

    std::optional<task_sender> io_task_tx{std::nullopt};
    std::atomic<int> io_worker_count{0};
    int io_worker_load{0};
    std::optional<task_sender> calcu_task_tx{std::nullopt};
    std::atomic<int> calcu_worker_count{0};
    int calcu_worker_load{0};

    task_id task_counter{1};
    std::unordered_map<void *, task_id> coro_to_task_id;

    sched::task to_task(task_instance task, bool is_blocking);
    void awake_all();

public:
    static runtime *get_runtime();

private:
    static runtime *current_runtime;
};

// Define macro `SET_RUNTIME` with set_runtime(rt)
// And use this before include future.h
#define set_runtime(rt) \
    using RT = rt

};

#endif
