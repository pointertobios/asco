// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <deque>
#include <stack>
#include <tuple>
#include <unordered_map>

#include <asco/concurrency/continuous_queue.h>
#include <asco/core/daemon.h>
#include <asco/core/task.h>
#include <asco/sync/spin.h>
#include <asco/utils/types.h>

namespace asco::core {

using namespace types;

namespace cq = continuous_queue;

using task_tuple = std::tuple<task_id, std::shared_ptr<task<>>>;
using task_sender = cq::sender<task_tuple>;
using task_receiver = cq::receiver<task_tuple>;

using awake_queue = spin<std::deque<size_t>>;

class worker final : public daemon {
public:
    worker(
        size_t id, atomic_size_t &load_counter, awake_queue &awake_q, task_receiver &&task_recv,
        atomic_size_t &worker_count, atomic_bool &shutting_down);
    ~worker();

    size_t id() const noexcept { return _id; }

    static bool in_worker() noexcept;
    static worker &this_worker() noexcept;

    void register_task(task_id id, std::shared_ptr<task<>> task, bool non_spawn = false);
    void unregister_task(task_id id);

    void task_enter(task_id id);
    task_id task_exit();

    task_id current_task();

    bool activate_task(task_id id);
    bool suspend_task(task_id id);

    std::shared_ptr<core::task<>> move_out_suspended_task(task_id id);
    void move_in_suspended_task(task_id id, std::shared_ptr<core::task<>> t);

private:
    bool init() override;
    bool run_once(std::stop_token &st) override;
    void shutdown() override;

    const size_t _id;
    task_receiver task_recv;

    atomic_size_t &load_counter;
    atomic_size_t &worker_count;
    atomic_bool &shutting_down;

    awake_queue &awake_q;

    std::stack<task_id> task_stack;

    spin<std::unordered_map<task_id, std::shared_ptr<task<>>>> tasks{};
    spin<std::unordered_map<task_id, std::shared_ptr<task<>>>> suspended_tasks{};
    spin<std::deque<std::shared_ptr<task<>>>> active_tasks{};

    std::tuple<task_id, std::shared_ptr<task<>>> sched();

    thread_local static atomic<worker *> _this_worker;
};

};  // namespace asco::core
