// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <semaphore>
#include <vector>

#include <asco/concurrency/hash_map.h>
#include <asco/core/cancellation.h>
#include <asco/core/daemon.h>
#include <asco/core/task/dynprio_scheduler.h>
#include <asco/core/task/execution_domain.h>
#include <asco/core/task/executor.h>
#include <asco/core/task/scheduler.h>
#include <asco/sync/spinlock.h>
#include <asco/this_task.h>
#include <asco/util/safe_erased.h>
#include <asco/yield.h>

namespace asco::core {

namespace detail {

struct coroutine_meta {
    std::coroutine_handle<> handle;
    std::atomic<task::execution_domain *> *pdomain_location;
    cancel_source *cancel_source;
    util::safe_erased tls;
    bool blocking;
};

struct task {
    std::uint64_t prio;
    std::vector<std::coroutine_handle<>> stack;

    bool operator<(const task &rhs) const { return prio > rhs.prio; }
};

static constexpr std::size_t coroutine_queue_capacity = 1024;
using coroutine_sender = concurrency::ring_queue::sender<coroutine_meta, coroutine_queue_capacity>;
using coroutine_receiver = concurrency::ring_queue::receiver<coroutine_meta, coroutine_queue_capacity>;
static constexpr auto coroutine_queue_create =
    concurrency::ring_queue::create<coroutine_meta, coroutine_queue_capacity>;

static constexpr std::size_t idle_workers_capacity = 1024;
using idle_workers_sender = concurrency::ring_queue::sender<std::size_t, idle_workers_capacity>;
using idle_workers_receiver = concurrency::ring_queue::receiver<std::size_t, idle_workers_capacity>;
static constexpr auto idle_workers_create =
    concurrency::ring_queue::create<std::size_t, idle_workers_capacity>;

};  // namespace detail

class runtime;

class worker final : public daemon {
    friend class runtime;
    friend void asco::this_task::close_cancellation() noexcept;
    friend cancel_token &asco::this_task::get_cancel_token() noexcept;
    template<typename TaskLocalStorage>
    friend TaskLocalStorage &asco::this_task::task_local() noexcept;
    friend bool asco::this_task::is_blocking_env() noexcept;

public:
    worker(
        std::size_t id, detail::coroutine_receiver rx,
        std::shared_ptr<std::counting_semaphore<detail::coroutine_queue_capacity + 1>> backsem,
        void *runtime_storage_ptr, void *runtime_ptr, detail::idle_workers_sender idle_tx);

    static worker &current();

    static worker *of_handle(std::coroutine_handle<> handle);

    static bool handle_valid(std::coroutine_handle<> handle);

    std::size_t id() const;

    void register_handle(std::coroutine_handle<> handle);
    void unregister_handle(std::coroutine_handle<> handle);

    task::scheduler &get_current_scheduler() noexcept { return m_domain_stack.back()->get_scheduler(); }
    task::execution_domain &get_current_execution_domain() noexcept { return *m_domain_stack.back(); }

    task::executor &get_executor() noexcept { return m_executor; }

private:
    bool init() override;
    bool run_once(std::stop_token &st) override;
    void shutdown() override;

    bool fetch_task();

    // 运行时上下文
    std::vector<task::execution_domain *> m_domain_stack;
    std::vector<task::scheduler_context *> m_context_stack;
    std::vector<task::scheduled_execution> m_sexec_stack;

    task::executor m_executor;

    task::dynprio_scheduler m_scheduler;
    task::execution_domain m_execution_domain;

    concurrency::hash_map<std::coroutine_handle<>, detail::coroutine_meta> m_coroutine_metas;

    const std::size_t m_id;

    detail::coroutine_receiver m_coroutine_rx;
    std::shared_ptr<std::counting_semaphore<detail::coroutine_queue_capacity + 1>> m_backsem;

    detail::idle_workers_sender m_idle_workers_tx;

    // 仅能在 init() 中安全使用
    void *m_runtime_storage_ptr;
    void *m_runtime_ptr;

    inline static concurrency::hash_map<std::coroutine_handle<>, worker *> *_corohandle_worker_map;

    inline thread_local static worker *_current_worker{nullptr};
};

};  // namespace asco::core

namespace asco::this_task {

template<typename TaskLocalStorage>
TaskLocalStorage &task_local() noexcept {
    auto &w = core::worker::current();
    if (auto exec = w.m_executor.current_execution()) {
        auto &tls = w.m_coroutine_metas.get(exec).value().tls;
        return tls.get<TaskLocalStorage>();
    } else {
        panic("asco::this_task::task_local: 当前没有正在运行的任务");
    }
}

};  // namespace asco::this_task
