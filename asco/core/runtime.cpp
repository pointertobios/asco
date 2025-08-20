// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/runtime.h>

#include <cassert>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>

#ifdef __linux__
#    include <pthread.h>
#    include <sched.h>
#endif

namespace asco::core {

rwspin<std::unordered_map<worker::task_id, std::binary_semaphore *>> worker::workers_by_task_id_sem;
std::unordered_map<worker::task_id, worker *> worker::workers_by_task_id;
std::unordered_map<std::thread::id, worker *> worker::workers;
thread_local worker *worker::current_worker{nullptr};
runtime *runtime::current_runtime{nullptr};

worker::worker(size_t id, const worker_fn &f, task_receiver rx)
        : id(id)
        , is_calculator(false)
        , task_rx(rx)
        , thread([f, this] { f(this); })
        , io_uring(id) {
    workers[thread.get_id()] = this;
}

worker::~worker() { thread.join(); }

std::thread::id worker::get_thread_id() const { return thread.get_id(); }

void worker::conditional_suspend() {
    if (task_rx.is_stopped())
        return;

    worker_await_sem.acquire();
}

void worker::awake() { worker_await_sem.release(); }

void worker::remove_task_map(task_id id) {
    auto guard = workers_by_task_id_sem.write();
    if (guard->find(id) == guard->end())
        return;
    guard->at(id)->acquire();
    workers_by_task_id.erase(id);
    guard->at(id)->release();
    delete guard->at(id);
    guard->erase(id);
}

void worker::insert_task_map(task_id id, worker *self) {
    auto guard = workers_by_task_id_sem.read();
    workers_by_task_id.emplace(id, self);
    guard->at(id)->release();
}

void worker::modify_task_map(task_id id, worker *self) {
    auto guard = workers_by_task_id_sem.read();
    if (!guard->contains(id))
        return;
    guard->at(id)->acquire();
    workers_by_task_id[id] = self;
    guard->at(id)->release();
}

bool worker::task_available(task_id id) {
    auto guard = workers_by_task_id_sem.read();
    if (auto its_ = guard->find(id); its_ != guard->end()) {
        auto its = its_->second;
        its->acquire();
        if (auto it = workers_by_task_id.find(id); it != workers_by_task_id.end()) {
            its->release();
            return true;
        } else {
            its->release();
            return false;
        }
    }
    return false;
}

// always promis the id is an exist id.
worker &worker::get_worker_from_task_id(task_id id) {
    if (!id)
        throw asco::runtime_error(
            "[ASCO] worker::get_worker_from_task_id() Inner error: unexpectedly got a 0 as task id");

    auto guard = workers_by_task_id_sem.read();
    if (auto its_ = guard->find(id); its_ != guard->end()) {
        auto its = its_->second;
        its->acquire();
        if (auto it = workers_by_task_id.find(id); it != workers_by_task_id.end()) {
            auto p = it->second;
            its->release();
            return *p;
        } else {
            its->release();
        }
    }
    throw asco::runtime_error("[ASCO] worker::get_worker_from_task_id() Inner error: task id does not exist");
}

void worker::set_task_sem(task_id id) {
    workers_by_task_id_sem.write()->emplace(std::make_pair(id, new std::binary_semaphore{0}));
}

bool worker::in_worker() { return workers.find(std::this_thread::get_id()) != workers.end(); }

worker &worker::get_worker() {
    if (workers.find(std::this_thread::get_id()) == workers.end())
        throw asco::runtime_error("[ASCO] worker::get_worker(): Currently not in any asco::worker thread");
    if (!current_worker)
        current_worker = workers[std::this_thread::get_id()];
    return *current_worker;
}

/* ---- runtime ---- */

std::vector<std::string> runtime::sys::__args;
std::unordered_map<std::string, std::string> runtime::sys::__env;

void runtime::sys::set_args(int argc, const char **argv) {
    __args.clear();
    for (int i = 0; i < argc; i++) { __args.push_back(argv[i]); }
}

void runtime::sys::set_env(const char **env) {
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
    for (size_t i{0}; i < std::thread::hardware_concurrency(); i++) {
        CPU_ZERO(&cpuset);
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
        throw asco::runtime_error("[ASCO] runtime::runtime(): Cannot create multiple runtimes in a process");
    current_runtime = this;

    auto worker_lambda = [this](worker *self_) {
        worker &self = *self_;

#ifdef __linux__
        ::pthread_setname_np(::pthread_self(), std::format("asco::worker{}", self.id).c_str());
        self.pid = ::gettid();
#endif

        while (!(self.task_rx.is_stopped() && self.sc.currently_finished_all())) {
            while (true)
                if (auto task = self.task_rx.try_recv(); task) {
                    self.sc.push_task(std::move(*task), scheduler::task_control::state::ready);
                    worker::insert_task_map(task->id, self_);
                } else
                    break;

            if (auto task = self.sc.sched(); task) {
                self.running_task.push(*task);
                if (!(*task)->done())
                    (*task)->resume();

                while (!self.running_task.empty()) {
                    auto task = self.running_task.top();
                    self.running_task.pop();

                    if (task->done()) {
                        if (self.is_calculator)
                            calcu_worker_load--;
                        else
                            io_worker_load--;
                        remove_task_map(task->handle.address());
                        self.remove_task_map(task->id);
                        self.sc.destroy(task->id);
                    } else {
                        if (self.task_schedulable(task->id))
                            self.sc.get_task(task->id).reset_real_time();
                    }
                }
            } else if (self.sc.has_buffered_awakes()) {
                self.sc.try_reawake_buffered();
            } else {
                self.conditional_suspend();
            }
        }
    };

    auto total_threads = nthread;
#ifndef ASCO_IO_URING
    if (nthread > 2)
        nthread -= 2;
#else
    if (nthread > 1)
        nthread -= 1;
#endif

    pool.reserve(nthread);
#ifdef __linux__
    auto cpus = get_cpus();

    if (total_threads > 2) {
        auto &timer_cpu = cpus[nthread];
        if (::sched_setaffinity(timer.native_thread_id(), sizeof(decltype(timer_cpu)), &timer_cpu) == -1) {
            throw asco::runtime_error("[ASCO] runtime::runtime(): Failed to set timer thread affinity");
        }

#    ifndef ASCO_IO_URING
        auto &io_cpu = cpus[nthread + 1];
        if (::sched_setaffinity(io.native_thread_id(), sizeof(decltype(io_cpu)), &io_cpu) == -1) {
            throw asco::runtime_error("[ASCO] runtime::runtime(): Failed to set io thread affinity");
        }
#    endif
    }
#endif

    auto [io_tx, io_rx] = inner::ms::channel<sched::task>();
    io_task_tx = std::move(io_tx);

    auto [calcu_tx, calcu_rx] = inner::ms::channel<sched::task>();
    calcu_task_tx = std::move(calcu_tx);

    for (size_t i{0}; i < nthread; i++) {
        bool is_calculator = false;

        // The hyper thread cores are usually the high frequency cores
        // Use them as calculator workers
#ifdef __linux__
        std::string path = std::format("/sys/devices/system/cpu/cpu{}/topology/thread_siblings_list", i);
        std::ifstream f(path);
        if (!f.is_open())
            throw asco::runtime_error("[ASCO] runtime::runtime(): Failed to detect CPU hyperthreading");
        std::string buf;
        std::vector<int> siblings;
        while (std::getline(f, buf, '-')) { siblings.push_back(std::atoi(buf.c_str())); }
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
            throw asco::runtime_error("[ASCO] runtime::runtime(): Failed to create worker");
        pool.push_back(p);
        auto &workeri = pool[i];

#ifdef __linux__
        auto &cpu = cpus[i];
        while (!workeri->pid);
        if (::sched_setaffinity(workeri->pid, sizeof(decltype(cpu)), &cpu) == -1) {
            throw asco::runtime_error("[ASCO] runtime::runtime(): Failed to set affinity");
        }
#endif

        workeri->is_calculator = is_calculator;
    }
}

runtime::~runtime() {
    io_task_tx->stop();
    calcu_task_tx->stop();
    awake_all();
    for (auto thread : pool) delete thread;
    current_runtime = nullptr;

#ifdef ASCO_PERF_RECORD
    std::cout << std::format(
        "[ASCO] Flag 'ASCO_PERF_RECORD' is enabled, please disable it if you are building your program for releasing.\n");

    std::cout << "\nactive\ttotal\tcounter\tname\n";
    auto record = perf::record::collect();
    std::vector<perf::record> res;
    for (auto &[id, rec] : record) { res.push_back(rec); }
    std::sort(res.begin(), res.end());
    for (auto rec : res) {
        auto &[name, total, active, counter] = rec;
        std::cout << std::format(
            "{}\t{}\t{}\t{}\n", std::chrono::duration_cast<std::chrono::milliseconds>(active),
            std::chrono::duration_cast<std::chrono::milliseconds>(total), counter, name);
    }
#endif
}

void runtime::awake_all() {
    for (auto worker : pool) { worker->awake(); }
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

runtime::task_id
runtime::spawn(task_instance task_, __coro_local_frame *pframe, unwind::coro_trace trace, task_id gid) {
    auto task = to_task(task_, false, pframe, trace);
    auto res = task.id;
    if (gid)
        join_task_to_group(res, gid, true);
    send_task(std::move(task));
    return res;
}

runtime::task_id runtime::spawn_blocking(
    task_instance task_, __coro_local_frame *pframe, unwind::coro_trace trace, task_id gid) {
    auto task = to_task(task_, true, pframe, trace);
    auto res = task.id;
    if (gid)
        join_task_to_group(res, gid, true);
    send_blocking_task(std::move(task));
    return res;
}

void runtime::awake(task_id id) {
    auto &worker = worker::get_worker_from_task_id(id);
    worker.sc.awake(id);
    worker.awake();
}

void runtime::suspend(task_id id) { worker::get_worker_from_task_id(id).sc.suspend(id); }

void runtime::abort(task_id id) {
    auto &w = worker::get_worker_from_task_id(id);
    auto &t = w.sc.get_task(id);
    t.aborted = true;
    if (auto wid = t.waiting.load(); wid) {
        abort(wid);
    } else {
        if (w.sc.get_state(id) != sched::std_scheduler::task_control::state::ready)
            awake(id);
    }
}

void runtime::register_sync_awaiter(task_id id) {
    worker::get_worker_from_task_id(id).sc.register_sync_awaiter(id);
}

void runtime::remove_task_map(void *addr) { coro_to_task_id.lock()->erase(addr); }

runtime::task_id runtime::task_id_from_corohandle(std::coroutine_handle<> handle) {
    auto guard = coro_to_task_id.lock();
    if (guard->find(handle.address()) == guard->end())
        throw asco::runtime_error(
            std::format(
                "[ASCO] runtime::task_id_from_corohandle() Inner error: coro_to_task_id[{}] unexists",
                handle.address()));
    return guard->at(handle.address());
}

sched::task
runtime::to_task(task_instance task, bool is_blocking, __coro_local_frame *pframe, unwind::coro_trace trace) {
    auto guard = coro_to_task_id.lock();
    auto id = task_counter.fetch_add(1, morder::relaxed);
    worker::set_task_sem(id);
    guard->insert(std::make_pair(task.address(), id));
    auto res = sched::task{id, task, new __coro_local_frame(pframe, trace), is_blocking};
#ifdef ASCO_PERF_RECORD
    res.perf_recorder = new perf::coro_recorder;
#endif
    return res;
}

void runtime::timer_attach(task_id id, std::chrono::high_resolution_clock::time_point time) {
    timer.attach(id, time);
}

void runtime::timer_detach(task_id id) {
    if (timer.task_attaching(id))
        timer.detach(id);
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
    throw asco::runtime_error(std::format("[ASCO] runtime::group(): task_group {} unexists", id));
}

};  // namespace asco::core
