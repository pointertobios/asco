// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cassert>
#include <coroutine>
#include <cstddef>
#include <deque>
#include <memory>
#include <semaphore>
#include <unordered_map>
#include <vector>

#include <asco/core/daemon.h>

namespace asco::core {

namespace detail {

static constexpr std::size_t coroutine_queue_capacity = 1024;
using coroutine_sender = concurrency::ring_queue::sender<std::coroutine_handle<>, coroutine_queue_capacity>;
using coroutine_receiver =
    concurrency::ring_queue::receiver<std::coroutine_handle<>, coroutine_queue_capacity>;
static constexpr auto coroutine_queue_create =
    concurrency::ring_queue::create<std::coroutine_handle<>, coroutine_queue_capacity>;

};  // namespace detail

class runtime;

class worker final : public daemon {
    friend class runtime;

public:
    worker(
        std::size_t id, detail::coroutine_receiver rx,
        std::shared_ptr<std::counting_semaphore<detail::coroutine_queue_capacity>> backsem,
        void *runtime_storage_ptr);
    ~worker() = default;

    static worker &current();

    static worker &of_handle(std::coroutine_handle<> handle);

    void awake_handle(std::coroutine_handle<> handle) noexcept;
    void suspend_handle(std::coroutine_handle<> handle) noexcept;

    void push_handle(std::coroutine_handle<> handle) noexcept;
    std::coroutine_handle<> pop_handle() noexcept;

private:
    bool init() override;
    bool run_once(std::stop_token &st) override;
    void shutdown() override;

    void fetch_task();

    std::vector<std::coroutine_handle<>> m_current_stack;
    std::deque<std::vector<std::coroutine_handle<>>> m_active_stacks;
    std::unordered_map<std::coroutine_handle<>, std::vector<std::coroutine_handle<>>> m_suspended_stacks;

    const std::size_t m_id;

    detail::coroutine_receiver m_coroutine_rx;
    std::shared_ptr<std::counting_semaphore<detail::coroutine_queue_capacity>> m_backsem;

    void *m_runtime_storage_ptr{nullptr};  // 仅能在 init() 中安全使用

    inline thread_local static worker *_current_worker{nullptr};
};

};  // namespace asco::core
