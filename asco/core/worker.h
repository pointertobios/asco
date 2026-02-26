// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <semaphore>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <asco/core/cancellation.h>
#include <asco/core/daemon.h>
#include <asco/sync/spinlock.h>
#include <asco/this_task.h>
#include <asco/util/safe_erased.h>
#include <asco/yield.h>

namespace asco::core {

namespace detail {

struct coroutine_meta {
    std::coroutine_handle<> handle;
    cancel_source *cancel_source;
    util::safe_erased tls;
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
    friend std::suspend_always asco::this_task::yield();
    friend void asco::this_task::close_cancellation() noexcept;
    friend cancel_token &asco::this_task::get_cancel_token() noexcept;
    template<typename TaskLocalStorage>
    friend TaskLocalStorage &asco::this_task::task_local() noexcept;

public:
    worker(
        std::size_t id, detail::coroutine_receiver rx,
        std::shared_ptr<std::counting_semaphore<detail::coroutine_queue_capacity>> backsem,
        void *runtime_storage_ptr, void *runtime_ptr, detail::idle_workers_sender idle_tx);
    ~worker() = default;

    static worker &current();

    static worker &of_handle(std::coroutine_handle<> handle);
    static worker *optional_of_handle(std::coroutine_handle<> handle);

    static bool handle_valid(std::coroutine_handle<> handle);

    std::size_t id() const;

    std::coroutine_handle<> this_coroutine() const;

    void awake_handle(std::coroutine_handle<> handle) noexcept;
    void suspend_current_handle(std::coroutine_handle<> handle) noexcept;

    void push_handle(std::coroutine_handle<> handle) noexcept;
    std::coroutine_handle<> pop_handle() noexcept;

    std::coroutine_handle<> top_of_join_handle(std::coroutine_handle<> handle) noexcept;

    void close_cancellation(std::coroutine_handle<> handle) noexcept;

private:
    bool init() override;
    bool run_once(std::stop_token &st) override;
    void shutdown() override;

    bool fetch_task();

    bool cancel_cleanup() noexcept;

    void yield_current();

    std::vector<std::coroutine_handle<>> m_current_task;
    cancel_token m_current_cancel_token;
    std::uint64_t m_current_task_time;
    std::uint64_t m_start_tsc;
    sync::spinlock<std::map<std::uint64_t, std::vector<std::coroutine_handle<>>>> m_active_tasks;
    sync::spinlock<std::unordered_map<std::coroutine_handle<>, std::coroutine_handle<>>> m_top_of_join_handle;
    sync::spinlock<std::unordered_map<std::coroutine_handle<>, std::vector<std::coroutine_handle<>>>>
        m_suspended_tasks;
    sync::spinlock<std::unordered_map<std::coroutine_handle<>, detail::coroutine_meta>> m_coroutine_metas;

    sync::spinlock<std::unordered_set<std::coroutine_handle<>>> m_preawake_handles;

    const std::size_t m_id;

    detail::coroutine_receiver m_coroutine_rx;
    std::shared_ptr<std::counting_semaphore<detail::coroutine_queue_capacity>> m_backsem;

    detail::idle_workers_sender m_idle_workers_tx;

    // 仅能在 init() 中安全使用
    void *m_runtime_storage_ptr;
    void *m_runtime_ptr;

    inline static sync::spinlock<std::unordered_map<std::coroutine_handle<>, worker *>>
        *_corohandle_worker_map;

    inline thread_local static worker *_current_worker{nullptr};
};

};  // namespace asco::core

namespace asco::this_task {

template<typename TaskLocalStorage>
TaskLocalStorage &task_local() noexcept {
    auto &w = core::worker::current();
    if (w.m_current_task.size()) {
        auto &tls = w.m_coroutine_metas.lock()->at(w.m_current_task.front()).tls;
        return tls.get<TaskLocalStorage>();
    } else {
        panic("asco::this_task::task_local: 当前没有正在运行的任务");
    }
}

};  // namespace asco::this_task
