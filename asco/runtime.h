#ifndef ASCO_RUNTIME_H
#define ASCO_RUNTIME_H

#include <atomic>
#include <coroutine>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <tuple>
#include <vector>

#include <asco/future_nocoro.h>
#include <asco/sched.h>
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

    explicit worker(int id, const worker_fn &f, task_receiver rx) noexcept;
    worker(const worker &) = delete;
    worker(worker &) = delete;
    worker(worker &&) = delete;
    ~worker();

    void join();
    std::thread::id get_thread_id() const;

    int id;
    int pid{0};
    bool is_calculator;
    task_receiver task_rx;
    scheduler sc;

private:
    std::jthread thread;

    bool moved{false};

private:
    static std::map<std::thread::id, worker *> workers;
    static worker *get_worker();
    thread_local static worker *current_worker;
};

template<typename T>
concept is_runtime = requires(T t) {
    typename T::scheduler;
    { T::get_runtime() } -> std::same_as<T *>;
    { t.spawn(task_instance{}) } -> std::same_as<size_t>;
    { t.spawn_blocking(task_instance{}) } -> std::same_as<size_t>;
    { t.awake(size_t{}) } -> std::same_as<void>;
} && sched::is_scheduler<typename T::scheduler>;

class runtime {
public:
    using scheduler = worker::scheduler;

    using task_id = sched::task::task_id;

    explicit runtime(int nthread = 0);
    explicit runtime(const runtime&) = delete;
    explicit runtime(const runtime&&) = delete;
    ~runtime();

    size_t spawn(task_instance task);
    size_t spawn_blocking(task_instance task);
    void awake(size_t id);

private:
    int nthread;
    std::vector<worker *> pool;

    std::optional<task_sender> io_task_tx{std::nullopt};
    std::atomic<int> io_worker_count{0};
    int io_worker_load{0};
    std::optional<task_sender> calcu_task_tx{std::nullopt};
    std::atomic<int> calcu_worker_count{0};
    int calcu_worker_load{0};

    sched::task to_task(task_instance task);

public:
    static runtime *get_runtime();

private:
    static runtime *current_runtime;
    task_id task_counter{1};
};

// Define macro `SET_RUNTIME` with set_runtime(rt)
// And use this before include future.h
#define set_runtime(rt) \
    using RT = rt

};

#endif
