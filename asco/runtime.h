#ifndef ASCO_RUNTIME_H
#define ASCO_RUNTIME_H

#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <tuple>
#include <vector>

#include <asco/utils/channel.h>
#include <asco/utils/utils.h>
#include <asco/future_nocoro.h>

using namespace asco_inner;

namespace asco {

class worker;
using worker_fn = std::function<void(size_t, worker *)>;

using task_sending_instance = std::function<void()>;
using task_sender = sender<task_sending_instance>;
using task_receiver = shared_receiver<task_sending_instance>;

class worker {
public:
    using native_handle_type = std::jthread::native_handle_type;

    explicit worker(int id, const worker_fn &f, task_receiver rx) noexcept;
    worker(const worker &) = delete;
    worker(worker &) = delete;
    worker(worker &&) = delete;
    ~worker();

    void join();
    native_handle_type native_handle();
    std::thread::id get_id() const;

    int pid{0};
    bool is_calculator;
    task_receiver task_rx;

private:
    std::jthread thread;
    int id;

    bool moved{false};

private:
    static std::map<std::thread::id, worker *> workers;
    static worker *get_worker();
};

class runtime {
public:
    explicit runtime(int nthread = 0);
    explicit runtime(const runtime&) = delete;
    explicit runtime(const runtime&&) = delete;
    ~runtime();

    // Spawn an io task
    // With load balancing. Not only spawn on io workers.
    template<typename T>
    requires is_move_secure_v<T>
    future_nocoro<T> spawn(future_nocoro<T> future) {
        if (io_worker_count
                && (io_worker_load * calcu_worker_count <= calcu_worker_load * io_worker_count
                || calcu_worker_count == 0)) {
            io_task_tx.value().send(std::move(future->get_workerf()));
        } else {
            calcu_task_tx.value().send(std::move(future->get_workerf()));
        }
        return std::move(future);
    }

    // Spawn an io task
    // With load balancing. Not only spawn on io workers.
    template<typename T>
    requires is_move_secure_v<T>
    future_nocoro<T> spawn(std::function<T()> f) {
        return spawn(__future_nocoro<T>::construct(f));
    }

    // Spawn a calculating task
    // With load balancing. Not only spawn on calculator workers.
    template<typename T>
    requires is_move_secure_v<T>
    future_nocoro<T> spawn_blocking(future_nocoro<T> future) {
        if (!io_worker_count
                || (calcu_worker_count
                && calcu_worker_load * io_worker_count < io_worker_load * calcu_worker_count
                && calcu_worker_count > 0)) {
            calcu_task_tx.value().send(std::move(future->get_workerf()));
        } else {
            io_task_tx.value().send(std::move(future->get_workerf()));
        }
        return std::move(future);
    }

    template<typename T>
    requires is_move_secure_v<T>
    future_nocoro<T> spawn_blocking(std::function<T()> f) {
        return spawn_blocking(__future_nocoro<T>::construct(f));
    }

    template<typename T>
    requires std::is_move_assignable_v<T> && std::is_move_constructible_v<T>
    T block_on(std::function<T()> f) {
        auto future = __future_nocoro<T>::construct(f);
        if (calcu_worker_count > 0) {
            calcu_task_tx.value().send(std::move(future->get_workerf()));
        } else {
            io_task_tx.value().send(std::move(future->get_workerf()));
        }
        return std::move(future->await());
    }

    template<typename T>
    requires std::is_integral_v<T> || std::is_pointer_v<T> || std::is_void_v<T>
    T block_on(std::function<T()> f) {
        auto future = __future_nocoro<T>::construct(f);
        if (calcu_worker_count > 0) {
            calcu_task_tx.value().send(std::move(future->get_workerf()));
        } else {
            io_task_tx.value().send(std::move(future->get_workerf()));
        }
        return future->await();
    }

private:
    int nthread;
    std::vector<worker *> pool;

    std::optional<task_sender> io_task_tx{std::nullopt};
    int io_worker_count{0};
    int io_worker_load{0};
    std::optional<task_sender> calcu_task_tx{std::nullopt};
    int calcu_worker_count{0};
    int calcu_worker_load{0};

private:
    static std::map<std::thread::id, runtime *> runtimes;
    static runtime *get_runtime();
};

};

#endif
