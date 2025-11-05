// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <cassert>
#include <format>
#include <memory>
#include <print>
#include <semaphore>
#ifdef __linux__
#    include <pthread.h>
#    include <sched.h>
#endif

#include <asco/core/worker.h>

#include <asco/concurrency/continuous_queue.h>
#include <asco/core/runtime.h>
#include <asco/utils/types.h>

namespace asco::core {

using namespace types;

thread_local atomic<worker *> worker::_this_worker{nullptr};

std::tuple<task_id, std::shared_ptr<task<>>> worker::sched() {
    if (auto act_g = active_tasks.lock(); !act_g->empty()) {
        auto t = std::move(act_g->front());
        act_g->pop_front();
        act_g->push_back(t);
        return {t->id, std::move(t)};
    }
    return {};
}

worker::worker(
    size_t id, atomic_size_t &load_counter, awake_queue &awake_q, task_receiver &&task_recv,
    atomic_size_t &worker_count)
        : daemon{std::format("asco::w{}", id)}
        , _id{id}
        , task_recv{std::move(task_recv)}
        , load_counter{load_counter}
        , worker_count{worker_count}
        , awake_q{awake_q} {
    daemon::start();
    init_sem.acquire();
}

worker::~worker() { _this_worker.store(nullptr, morder::release); }

bool worker::init() {
#ifdef __linux__
    {
        ::cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(_id, &cpuset);
        if (::pthread_setaffinity_np(::pthread_self(), sizeof(cpuset), &cpuset) == -1) {
            std::println(stderr, "[ASCO] worker::init(): Failed to set thread affinity.");
            return false;
        }
    }
#endif

    _this_worker.store(this, morder::release);
    awake_q.lock()->push_back(_id);

    init_sem.release();
    return true;
}

bool worker::run_once() {
    while (true) {
        if (auto res = task_recv.pop()) {
            auto [id, task] = std::move(*res);
            task->worker_ptr = this;
            task->scheduled.store(true, morder::release);
            register_task(id, std::move(task));
        } else if (res.error() == cq::pop_fail::non_object) {
            break;
        } else if (res.error() == cq::pop_fail::closed) {
            return false;
        }
    }

    auto [id, task] = sched();
    if (!id) {
        sleep_until_awake_for();
        awake_q.lock()->push_back(_id);
        return true;
    }

    task_stack = std::move(task->call_chain);
    task->corohandle.resume();
    tasks.lock()->at(task_stack.top())->call_chain = std::move(task_stack);

    if (task->spawn_task && task->corohandle.done()) {
        // The tasks always suspend themselves, so only unregister_task is OK.
        core::runtime::this_runtime().unregister_task(id);
        load_counter.fetch_sub(1, morder::acq_rel);
        unregister_task(id);
        task->corohandle.destroy();
    }

    return true;
}

void worker::shutdown() {
    if (worker_count.fetch_sub(1, morder::release) == 1)
        worker_count.notify_one();
}

bool worker::in_worker() noexcept { return _this_worker.load(morder::acquire) != nullptr; }

worker &worker::this_worker() noexcept { return *_this_worker.load(morder::acquire); }

void worker::register_task(task_id id, std::shared_ptr<task<>> task, bool non_spawn) {
    tasks.lock()->emplace(id, task);
    if (non_spawn) {
        suspended_tasks.lock()->emplace(id, std::move(task));
    } else {
        active_tasks.lock()->emplace_back(std::move(task));
    }
}

void worker::unregister_task(task_id id) {
    tasks.lock()->erase(id);
    if (auto g = suspended_tasks.lock()) {
        auto it = g->find(id);
        if (it != g->end()) {
            g->erase(it);
        }
    }
}

void worker::task_enter(task_id id) { task_stack.push(id); }

task_id worker::task_exit() {
    auto id = task_stack.top();
    task_stack.pop();
    return id;
}

task_id worker::current_task() { return task_stack.top(); }

bool worker::activate_task(task_id id) {
    std::shared_ptr<task<>> t;
    if (auto susp_g = suspended_tasks.lock()) {
        if (susp_g->empty()) {
            return false;
        } else if (auto it = susp_g->find(id); it != susp_g->end()) {
            t = std::move(it->second);
            susp_g->erase(it);
        }
    }

    if (!t)
        return false;

    active_tasks.lock()->push_back(std::move(t));
    awake();
    return true;
}

bool worker::suspend_task(task_id id) {
    std::shared_ptr<task<>> t;
    if (auto act_g = active_tasks.lock()) {
        for (auto it = act_g->rbegin(); it != act_g->rend(); ++it) {
            if ((*it)->id == id) {
                t = std::move(*it);
                act_g->erase(act_g->begin() + (act_g->rend() - it - 1));
                break;
            }
        }
    }
    if (!t)
        return false;

    suspended_tasks.lock()->emplace(id, std::move(t));
    return true;
}

std::shared_ptr<core::task<>> worker::move_out_suspended_task(task_id id) {
    std::shared_ptr<task<>> t;
    if (auto susp_g = suspended_tasks.lock()) {
        auto it = susp_g->find(id);
        if (it != susp_g->end()) {
            t = std::move(it->second);
            susp_g->erase(it);
        }
    }
    if (t) {
        t->worker_ptr = nullptr;
        t->scheduled.store(false, morder::release);
        tasks.lock()->erase(id);
    }
    return t;
}

void worker::move_in_suspended_task(task_id id, std::shared_ptr<core::task<>> t) {
    t->worker_ptr = this;
    t->scheduled.store(true, morder::release);
    suspended_tasks.lock()->emplace(id, t);
    tasks.lock()->emplace(id, std::move(t));
}

};  // namespace asco::core
