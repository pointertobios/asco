#include <asco/runtime.h>

#include <string>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <pthread.h>

#ifdef __linux__
    #include <sched.h>
    #include <cpuid.h>
    #include <sys/syscall.h>
#endif

#include <asco/sched.h>

namespace asco {
    std::unordered_map<worker::task_id, worker *> worker::workers_by_task_id;
    std::unordered_map<std::thread::id, worker *> worker::workers;
    thread_local worker *worker::current_worker{nullptr};
    runtime *runtime::current_runtime{nullptr};

    worker::worker(int id, const worker_fn &f, task_receiver rx) noexcept
        : id(id)
        , is_calculator(false)
        , task_rx(rx)
        , thread([f, this]() {
            f(this);
        }) {
        workers[thread.get_id()] = this;
    }

    worker::~worker() {
        join();
    }

    void worker::join() {
        if (!moved)
            thread.join();
    }

    std::thread::id worker::get_thread_id() const {
        return thread.get_id();
    }

    void worker::conditional_suspend() {
        if (task_rx->is_stopped())
            return;

        if (!awake_signal) {
            std::unique_lock lk(cv_mutex);
            suspending = true;
            cv.wait(lk, [this] { return awake_signal; });
            suspending = false;
            lk.unlock();
        }
        awake_signal = false;
    }

    void worker::awake() {
        awake_signal = true;
        if (suspending) {
            cv.notify_one();
        }
    }

    // always promis the id is an exist id.
    worker *worker::get_worker_from_task_id(task_id id) {
        if (auto it = workers_by_task_id.find(id); it != workers_by_task_id.end()) {
            return it->second;
        }
        while (true) {
            for (auto [_, pworker] : workers) {
                if (pworker->sc.task_exists(id)) {
                    workers_by_task_id[id] = pworker;
                    return pworker;
                }
            }
        }
    }

    worker *worker::get_worker() {
        if (workers.find(std::this_thread::get_id()) == workers.end())
            throw std::runtime_error("[ASCO] Currently not in any asco::worker thread");
        if (!current_worker)
            current_worker = workers[std::this_thread::get_id()];
        return current_worker;
    }

    /* ---- runtime ---- */

#ifdef __linux__
    static std::vector<cpu_set_t> get_cpus() {
        std::vector<cpu_set_t> cpus;
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        for (int i = 0; i < std::thread::hardware_concurrency(); i++) {
            CPU_SET(i, &cpuset);
            cpus.push_back(cpuset);
        }
        return cpus;
    }
#endif

    runtime::runtime(int nthread_)
            : nthread(nthread_ ? nthread_ : std::thread::hardware_concurrency()) {
        if (current_runtime)
            throw std::runtime_error("[ASCO] Caonnot create multiple runtimes in a process");
        current_runtime = this;

        auto worker_lambda = [this](worker *self_) {
            worker &self = *self_;

            pthread_setname_np(pthread_self(), std::format("asco::worker{}", self.id).c_str());
#ifdef __linux__
            self.pid = syscall(SYS_gettid);
#endif

            while (true) {
                if (self.task_rx->is_stopped() && self.sc.currently_finished_all())
                    break;

                while (true) if (auto task = self.task_rx->try_recv(); task) {
                    self.sc.push_task(std::move(*task));
                } else break;

                if (auto task = self.sc.sched(); task) {
                    task->resume();
                    if (task->done()) {
                        if (self.is_calculator)
                            calcu_worker_load--;
                        else
                            io_worker_load--;
                        if (auto it = self.sync_awaiters_tx.find(task->id);
                                it != self.sync_awaiters_tx.end()) {
                            it->second.send(0);
                        }
                        coro_to_task_id.erase(task->handle.address());
                        worker::workers_by_task_id.erase(task->id);
                        self.sc.destroy(task->id);
                    }
                }

                self.conditional_suspend();
            }
        };

        pool.reserve(nthread);
#ifdef __linux__
        auto cpus = get_cpus();
#endif

        auto [io_tx, io_rx_] = channel<sched::task>(128);
        io_task_tx = std::move(io_tx);
        task_receiver io_rx = make_shared_receiver(std::move(io_rx_));

        auto [calcu_tx, calcu_rx_] = channel<sched::task>(128);
        calcu_task_tx = std::move(calcu_tx);
        task_receiver calcu_rx = make_shared_receiver(std::move(calcu_rx_));

        for (int i = 0; i < nthread; i++) {
            bool is_calculator = false;

            // The hyper thread cores are usually the high frequency cores
            // Use them as calculator workers
#ifdef __linux__
            std::string path = std::format("/sys/devices/system/cpu/cpu{}/topology/thread_siblings_list", i % cpus.size());
            std::ifstream f(path);
            if (!f.is_open())
                throw std::runtime_error("[CoIO] Failed to detect CPU hyperthreading");
            std::string buf;
            std::vector<int> siblings;
            while (std::getline(f, buf, '-')) {
                siblings.push_back(std::atoi(buf.c_str()));
            }
            f.close();
            if (siblings.size() > 1) {
                is_calculator = true;
            }
#endif

            if (is_calculator) {
                calcu_worker_count++;
            } else {
                io_worker_count++;
            }

            task_receiver rx;
            if (is_calculator) {
                rx = calcu_rx;
            } else {
                rx = io_rx;
            }
            auto *p = new worker(i, worker_lambda, rx);
            if (!p)
                throw std::runtime_error("[ASCO] Failed to create worker");
            pool.push_back(p);
            auto &workeri = pool[i];

#ifdef __linux__
            auto &cpu = cpus[i % cpus.size()];
            while (!workeri->pid);
            if (sched_setaffinity(
                workeri->pid,
                sizeof(decltype(cpu)),
                &cpu
            ) == -1) {
                throw std::runtime_error("[ASCO] Failed to set affinity");
            }
            
            workeri->is_calculator = is_calculator;
#endif
        }
    }

    runtime::~runtime() {
        io_task_tx->stop();
        calcu_task_tx->stop();
        awake_all();
        for (auto thread : pool) {
            thread->join();
        }
    }

    void runtime::awake_all() {
        for (auto worker : pool) {
            worker->awake();
        }
    }

    runtime::task_id runtime::spawn(task_instance task_) {
        auto task = to_task(task_, false);
        auto res = task.id;
        if (io_worker_count
                && io_worker_count * calcu_worker_load <= calcu_worker_count * io_worker_count) {
            io_worker_count++;
            io_task_tx->send(task);
        } else {
            calcu_worker_count++;
            calcu_task_tx->send(task);
        }
        awake_all();
        return res;
    }

    runtime::task_id runtime::spawn_blocking(task_instance task_) {
        auto task = to_task(task_, true);
        auto res = task.id;
        if (io_worker_count
                && io_worker_count * calcu_worker_load < calcu_worker_count * io_worker_count) {
            io_worker_count++;
            io_task_tx->send(task);
        } else {
            calcu_worker_count++;
            calcu_task_tx->send(task);
        }
        awake_all();
        return res;
    }

    void runtime::awake(task_id id) {
        auto *worker = worker::get_worker_from_task_id(id);
        worker->sc.awake(id);
        worker->awake();
    }

    void runtime::suspend(task_id id) {
        worker::get_worker_from_task_id(id)->sc.suspend(id);
    }

    sync_awaiter runtime::register_sync_awaiter(task_id id) {
        auto [tx, rx] = channel<__u8>(1);
        worker::get_worker_from_task_id(id)->sync_awaiters_tx[id] = std::move(tx);
        return sync_awaiter{make_shared_receiver(std::move(rx))};
    }

    runtime::task_id runtime::task_id_from_corohandle(std::coroutine_handle<> handle) {
        if (coro_to_task_id.find(handle.address()) == coro_to_task_id.end())
            throw std::runtime_error(std::format("[ASCO] runtime::task_id runtime::task_id_from_corohandle() Inner error: coro_to_task_id[{}] unexists", handle.address()));
        return coro_to_task_id[handle.address()];
    }

    sched::task runtime::to_task(task_instance task, bool is_blocking) {
        auto id = task_counter++;
        coro_to_task_id[task.address()] = id;
        return sched::task{id, task, is_blocking};
    }

    runtime *runtime::get_runtime() {
        if (!current_runtime)
            throw std::runtime_error("The async function must be called with asco::runtime initialized");
        return current_runtime;
    }


};
