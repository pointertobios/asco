// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <asco/core/runtime.h>

#include <cassert>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <ranges>
#include <string>
#include <string_view>

#ifdef __linux__
#    include <cpuid.h>
#    include <pthread.h>
#    include <sched.h>
#endif

#include <asco/core/sched.h>

namespace asco {
std::map<worker::task_id, std::binary_semaphore *> worker::workers_by_task_id_sem;
std::mutex worker::wsem_mutex;
std::map<worker::task_id, worker *> worker::workers_by_task_id;
std::unordered_map<std::thread::id, worker *> worker::workers;
thread_local worker *worker::current_worker{nullptr};
runtime *runtime::current_runtime{nullptr};

worker::worker(int id, const worker_fn &f, task_receiver rx)
        : id(id)
        , is_calculator(false)
        , task_rx(rx)
        , thread([f, this] { f(this); }) {
    workers[thread.get_id()] = this;
}

worker::~worker() { join(); }

void worker::join() {
    if (!moved)
        thread.join();
}

std::thread::id worker::get_thread_id() const { return thread.get_id(); }

void worker::conditional_suspend() {
    if (task_rx->is_stopped())
        return;

    worker_await_sem.acquire();
}

void worker::awake() { worker_await_sem.release(); }

void worker::remove_task_map(task_id id) {
    std::lock_guard lk{wsem_mutex};
    if (workers_by_task_id_sem.find(id) == workers_by_task_id_sem.end())
        return;
    workers_by_task_id_sem.at(id)->acquire();
    workers_by_task_id.erase(id);
    workers_by_task_id_sem.at(id)->release();
    delete workers_by_task_id_sem.at(id);
    workers_by_task_id_sem.erase(id);
}

void worker::insert_task_map(task_id id, worker *self) {
    std::lock_guard lk{wsem_mutex};
    workers_by_task_id.emplace(id, self);
    workers_by_task_id_sem.at(id)->release();
}

// always promis the id is an exist id.
worker &worker::get_worker_from_task_id(task_id id) {
    if (!id)
        throw std::runtime_error(
            "[ASCO] worker::get_worker_from_task_id() Inner error: unexpectedly got a 0 as task id");

    bool b = false;
    if (auto its_ = workers_by_task_id_sem.find(id); its_ != workers_by_task_id_sem.end()) {
        auto its = its_->second;
        b = true;
        its->acquire();
        if (auto it = workers_by_task_id.find(id); it != workers_by_task_id.end()) {
            auto p = it->second;
            if (b)
                its->release();
            return *p;
        } else {
            if (b)
                its->release();
            throw std::runtime_error("[ASCO] Inner error: task id does not exist");
        }
    }
    throw std::runtime_error(
        "[ASCO] worker::get_worker_from_task_id() Inner error: task id does not exist in sems");
}

void worker::set_task_sem(task_id id) {
    std::lock_guard<std::mutex> lock{wsem_mutex};
    workers_by_task_id_sem.emplace(std::make_pair(id, new std::binary_semaphore{0}));
}

bool worker::in_worker() { return workers.find(std::this_thread::get_id()) != workers.end(); }

worker &worker::get_worker() {
    if (workers.find(std::this_thread::get_id()) == workers.end())
        throw std::runtime_error("[ASCO] worker::get_worker(): Currently not in any asco::worker thread");
    if (!current_worker)
        current_worker = workers[std::this_thread::get_id()];
    return *current_worker;
}

/* ---- runtime ---- */

std::vector<std::string> runtime::sys::__args;
std::unordered_map<std::string, std::string> runtime::sys::__env;

void runtime::sys::set_args(int argc, const char **argv) {
    __args.clear();
    for (int i = 0; i < argc; i++) {
        __args.push_back(argv[i]);
    }
}

void runtime::sys::set_env(char **env) {
    for (; *env; env++) {
        std::string_view env_str = *env;
        auto i = env_str.find_first_of('=');
        std::string name(env_str.substr(0, i));
        std::string value(env_str.substr(i + 1));
        __env[name] = value;
    }
}

#ifdef __linux__
static std::vector<cpu_set_t> get_cpus() {
    std::vector<cpu_set_t> cpus;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (size_t i{0}; i < std::thread::hardware_concurrency(); i++) {
        CPU_SET(i, &cpuset);
        cpus.push_back(cpuset);
    }
    return cpus;
}
#endif

runtime::runtime(size_t nthread_)
        : nthread(
              (nthread_ > 0 && nthread_ <= std::thread::hardware_concurrency())
                  ? nthread_
                  : std::thread::hardware_concurrency()) {
    if (current_runtime)
        throw std::runtime_error("[ASCO] runtime::runtime(): Cannot create multiple runtimes in a process");
    current_runtime = this;

    auto worker_lambda = [this](worker *self_) {
        worker &self = *self_;

#ifdef __linux__
        ::pthread_setname_np(::pthread_self(), std::format("asco::worker{}", self.id).c_str());
        self.pid = gettid();
#endif

        while (!(self.task_rx->is_stopped() && self.sc.currently_finished_all())) {
            while (true)
                if (auto task = self.task_rx->try_recv(); task) {
                    self.sc.push_task(std::move(*task), scheduler::task_control::__control_state::suspending);
                    worker::insert_task_map(task->id, self_);
                } else
                    break;

            if (auto task = self.sc.sched(); task) {
                self.running_task.push(*task);
                try {
                    if (!(*task)->done())
                        (*task)->resume();
                } catch (caller_destroyed &) {
                } catch (std::exception &e) {
                    std::cerr << std::format(
                        "[ASCO] worker thread: Inner error at task {}: {}\n", (*task)->id, e.what());
                    break;
                }

                while (!self.running_task.empty()) {
                    auto task = self.running_task.top();
                    self.running_task.pop();

                    if (task->done()) {
                        if (!task->is_inline) {  // Inline task remove map relation themselves
                            remove_task_map(task->handle.address());
                            self.remove_task_map(task->id);
                        }

                        if (self.is_calculator)
                            calcu_worker_load--;
                        else
                            io_worker_load--;

                        self.sc.destroy(task->id);

                    } else {
                        try {
                            // The map of inline task might already been destroyed by future::promise_type, so
                            // get_task() throw an exception. Just ignore it.
                            self.sc.get_task(task->id).reset_real_time();
                        } catch (...) {
                        }
                    }
                }
            } else if (self.sc.has_buffered_awakes()) {
                self.sc.try_reawake_buffered();
            } else {
                self.conditional_suspend();
            }
        }
    };

    pool.reserve(nthread);
#ifdef __linux__
    auto cpus = get_cpus();
#endif

    auto [io_tx, io_rx_] = inner::channel<sched::task>();
    io_task_tx = std::move(io_tx);
    task_receiver io_rx = make_shared_receiver(std::move(io_rx_));

    auto [calcu_tx, calcu_rx_] = inner::channel<sched::task>();
    calcu_task_tx = std::move(calcu_tx);
    task_receiver calcu_rx = make_shared_receiver(std::move(calcu_rx_));

    for (int i = 0; i < nthread; i++) {
        bool is_calculator = false;

        // The hyper thread cores are usually the high frequency cores
        // Use them as calculator workers
#ifdef __linux__
        std::string path =
            std::format("/sys/devices/system/cpu/cpu{}/topology/thread_siblings_list", i % cpus.size());
        std::ifstream f(path);
        if (!f.is_open())
            throw std::runtime_error("[ASCO] runtime::runtime(): Failed to detect CPU hyperthreading");
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
            throw std::runtime_error("[ASCO] runtime::runtime(): Failed to create worker");
        pool.push_back(p);
        auto &workeri = pool[i];

#ifdef __linux__
        auto &cpu = cpus[i % cpus.size()];
        while (!workeri->pid);
        if (::sched_setaffinity(workeri->pid, sizeof(decltype(cpu)), &cpu) == -1) {
            throw std::runtime_error("[ASCO] runtime::runtime(): Failed to set affinity");
        }
#endif

        workeri->is_calculator = is_calculator;
    }
}

runtime::~runtime() {
    io_task_tx->stop();
    calcu_task_tx->stop();
    awake_all();
    for (auto thread : pool) {
        thread->join();
    }
    current_runtime = nullptr;
}

void runtime::awake_all() {
    for (auto worker : pool) {
        worker->awake();
    }
}

void runtime::send_task(sched::task task) {
    if (io_worker_count && io_worker_count * calcu_worker_load <= calcu_worker_count * io_worker_load) {
        io_worker_load++;
        auto _ = io_task_tx->send(task);
    } else {
        calcu_worker_load++;
        auto _ = calcu_task_tx->send(task);
    }
    awake_all();
}

void runtime::send_blocking_task(sched::task task) {
    if (calcu_worker_count && io_worker_count * calcu_worker_load <= calcu_worker_count * io_worker_load) {
        calcu_worker_load++;
        auto _ = calcu_task_tx->send(task);
    } else {
        io_worker_load++;
        auto _ = io_task_tx->send(task);
    }
    awake_all();
}

runtime::task_id runtime::spawn(task_instance task_, __coro_local_frame *pframe, task_id gid) {
    auto task = to_task(task_, false, pframe);
    auto res = task.id;
    if (gid)
        join_task_to_group(res, gid, true);
    send_task(task);
    return res;
}

runtime::task_id runtime::spawn_blocking(task_instance task_, __coro_local_frame *pframe, task_id gid) {
    auto task = to_task(task_, true, pframe);
    auto res = task.id;
    if (gid)
        join_task_to_group(res, gid, true);
    send_blocking_task(task);
    return res;
}

void runtime::awake(task_id id) {
    auto &worker = worker::get_worker_from_task_id(id);
    worker.sc.awake(id);
    worker.awake();
}

void runtime::suspend(task_id id) { worker::get_worker_from_task_id(id).sc.suspend(id); }

void runtime::abort(task_id id) {
    if (timer.task_attaching(id))
        timer.detach(id);
    auto &w = worker::get_worker_from_task_id(id);
    auto &t = w.sc.get_task(id);
    t.aborted = true;
    if (t.waiting) {
        abort(t.waiting);
        awake(t.waiting);
    }
}

void runtime::register_sync_awaiter(task_id id) {
    worker::get_worker_from_task_id(id).sc.register_sync_awaiter(id);
}

void runtime::remove_task_map(void *addr) {
    std::lock_guard lk{coro_to_task_id_mutex};
    coro_to_task_id.erase(addr);
}

runtime::task_id runtime::task_id_from_corohandle(std::coroutine_handle<> handle) {
    std::lock_guard lk{coro_to_task_id_mutex};
    if (coro_to_task_id.find(handle.address()) == coro_to_task_id.end())
        throw std::runtime_error(
            std::format(
                "[ASCO] runtime::task_id_from_corohandle() Inner error: coro_to_task_id[{}] unexists",
                handle.address()));
    return coro_to_task_id[handle.address()];
}

sched::task runtime::to_task(task_instance task, bool is_blocking, __coro_local_frame *pframe) {
    std::lock_guard lk{coro_to_task_id_mutex};
    auto id = task_counter.fetch_add(1, morder::relaxed);
    worker::set_task_sem(id);
    coro_to_task_id.insert(std::make_pair(task.address(), id));
    auto res = sched::task{id, task, new __coro_local_frame(pframe), is_blocking};
    return res;
}

void runtime::timer_attach(task_id id, std::chrono::high_resolution_clock::time_point time) {
    timer.attach(id, time);
}

void runtime::join_task_to_group(task_id id, task_id gid, bool origin) {
    auto guard = task_groups.lock();
    if (auto it = guard->find(gid); it == guard->end())
        guard->emplace(gid, new task_group{});

    (*guard)[gid]->add_task(id, origin);
    guard->emplace(id, (*guard)[gid]);
}

void runtime::exit_group(task_id id) {
    auto guard = task_groups.lock();
    if (auto it = guard->find(id); it != guard->end()) {
        task_id another_erase = 0;
        if (auto task = it->second->remove_task(id); task) {
            another_erase = *task;
            delete it->second;
        }
        guard->erase(it);
        if (another_erase)
            guard->erase(another_erase);
    }
}

bool runtime::in_group(task_id id) {
    auto guard = task_groups.lock();
    auto it = guard->find(id);
    return it != guard->end();
}

task_group *runtime::group(task_id id) {
    auto guard = task_groups.lock();
    if (auto it = guard->find(id); it != guard->end())
        return it->second;
    throw std::runtime_error(std::format("[ASCO] runtime::group(): task_group {} unexists", id));
}

};  // namespace asco
