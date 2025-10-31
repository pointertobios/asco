// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <format>
#include <fstream>
#include <thread>

#include <asco/core/runtime.h>

#include <asco/compile_time/platform.h>
#include <asco/core/worker.h>
#include <asco/panic/panic.h>

namespace asco::core {

using compile_time::platform::os;
using compile_time::platform::platform;

runtime &runtime::init(size_t parallel) noexcept {
    static runtime instance{parallel};
    return instance;
}

runtime &runtime::this_runtime() noexcept { return *_this_runtime; }

void runtime::register_task(task_id id, std::shared_ptr<task<>> task) {
    tasks_by_handle.write()->emplace(task->corohandle, task);
    task_ids_by_handle.write()->emplace(task->corohandle, id);
    tasks_by_id.write()->emplace(id, std::move(task));
}

void runtime::unregister_task(task_id id) {
    auto tasks_by_handle_g = tasks_by_handle.write();
    auto task_ids_by_handle_g = task_ids_by_handle.write();
    auto tasks_by_id_g = tasks_by_id.write();
    auto it = tasks_by_id_g->find(id);
    if (it != tasks_by_id_g->end()) {
        auto handle = it->second->corohandle;
        tasks_by_handle_g->erase(handle);
        task_ids_by_handle_g->erase(handle);
        tasks_by_id_g->erase(it);
    }
}

std::shared_ptr<task<>> runtime::get_task_by(std::coroutine_handle<> handle) {
    auto tasks_by_handle_g = tasks_by_handle.read();
    auto it = tasks_by_handle_g->find(handle);
    if (it != tasks_by_handle_g->end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<task<>> runtime::get_task_by(task_id id) {
    auto tasks_by_id_g = tasks_by_id.read();
    auto it = tasks_by_id_g->find(id);
    if (it != tasks_by_id_g->end()) {
        return it->second;
    }
    return nullptr;
}

task_id runtime::get_task_id_by(std::coroutine_handle<> handle) {
    auto task_ids_by_handle_g = task_ids_by_handle.read();
    auto it = task_ids_by_handle_g->find(handle);
    if (it != task_ids_by_handle_g->end()) {
        return it->second;
    }
    return task_id{};
}

void runtime::spawn_task(task_id id, std::shared_ptr<task<>> task) {
    auto pan = [] [[noreturn]] {
        panic::co_panic(
            "[ASCO] runtime::spawn_task(): Failed to send task. The task queue unexpectedly closed.");
    };

    if (io_worker_count && io_worker_count * calcu_worker_load <= calcu_worker_count * io_worker_load) {
        io_worker_load.fetch_add(1, morder::acq_rel);
        if (io_task_tx.push({id, std::move(task)}))
            pan();
        awake_io_worker_once();
    } else {
        calcu_worker_load.fetch_add(1, morder::acq_rel);
        if (calcu_task_tx.push({id, std::move(task)}))
            pan();
        awake_calcu_worker_once();
    }
}

void runtime::spawn_core_task(task_id id, std::shared_ptr<task<>> task) {
    auto pan = [] [[noreturn]] {
        panic::co_panic(
            "[ASCO] runtime::spawn_core_task(): Failed to send task. The task queue unexpectedly closed.");
    };

    if (calcu_worker_count && io_worker_count * calcu_worker_load <= calcu_worker_count * io_worker_load) {
        calcu_worker_load.fetch_add(1, morder::acq_rel);
        if (calcu_task_tx.push({id, std::move(task)}))
            pan();
        awake_calcu_worker_once();
    } else {
        io_worker_load.fetch_add(1, morder::acq_rel);
        if (io_task_tx.push({id, std::move(task)}))
            pan();
        awake_io_worker_once();
    }
}

void runtime::awake_all() {
    for (auto &w : workers) { w->awake(); }
}

void runtime::awake_io_worker_once() {
    size_t wid{0};
    bool found{false};
    if (auto g = io_worker_queue.lock(); !g->empty()) {
        wid = g->front();
        g->pop_front();
        found = true;
    }
    if (found)
        workers[wid]->awake();
    else
        awake_all();
}

void runtime::awake_calcu_worker_once() {
    size_t wid{0};
    bool found{false};
    if (auto g = calcu_worker_queue.lock(); !g->empty()) {
        wid = g->front();
        g->pop_front();
        found = true;
    }
    if (found)
        workers[wid]->awake();
    else
        awake_all();
}

runtime::runtime(size_t parallel) {
    if (parallel == 0) {
        parallel = std::thread::hardware_concurrency();
        if (parallel == 0)
            parallel = 4;
    }

    _this_runtime = this;

    workers.reserve(parallel);
    worker_count.store(parallel, morder::release);

    auto [iotx, iorx] = cq::create<task_tuple>();
    auto [caltx, calrx] = cq::create<task_tuple>();

    io_task_tx = std::move(iotx);
    calcu_task_tx = std::move(caltx);

    for (size_t i = 0; i < parallel; ++i) {
        bool is_calculator{false};
        // The hyper thread cores are usually the high frequency cores Use them as calculator workers
        if constexpr (platform::os_is(os::linux)) {
            try {
                std::string path =
                    std::format("/sys/devices/system/cpu/cpu{}/topology/thread_siblings_list", i);
                std::ifstream f(path);
                if (!f.is_open())  // If failed, use this worker as IO worker (is_calculator == false)
                    goto hyperthreading_detected;
                std::string buf;
                std::vector<int> siblings;
                while (std::getline(f, buf, '-')) { siblings.push_back(std::atoi(buf.c_str())); }
                f.close();
                if (siblings.size() > 1) {
                    is_calculator = true;
                }
            } catch (...) {
                // If failed, use this worker as IO worker (is_calculator == false)
                goto hyperthreading_detected;
            }
        }

    hyperthreading_detected:

        task_receiver rx = is_calculator ? calrx : iorx;
        awake_queue &wq = is_calculator ? calcu_worker_queue : io_worker_queue;
        atomic_size_t &lc = is_calculator ? calcu_worker_load : io_worker_load;

        if (is_calculator) {
            calcu_worker_count++;
        } else {
            io_worker_count++;
        }

        workers.push_back(new worker{i, lc, wq, std::move(rx), worker_count});
    }
}

runtime::~runtime() {
    io_task_tx.stop();
    calcu_task_tx.stop();
    awake_all();
    worker_count.wait(0);
    for (auto w : workers) { delete w; }
}

};  // namespace asco::core
