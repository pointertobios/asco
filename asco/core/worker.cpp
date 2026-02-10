// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/worker.h>

#include <coroutine>
#include <format>

#include <asco/core/runtime.h>

namespace asco::core {

worker::worker(
    std::size_t id, detail::coroutine_receiver rx,
    std::shared_ptr<std::counting_semaphore<detail::coroutine_queue_capacity>> backsem,
    void *runtime_storage_ptr)
        : daemon(std::format("asco-w{}", id))
        , m_id{id}
        , m_coroutine_rx{std::move(rx)}
        , m_backsem{std::move(backsem)}
        , m_runtime_storage_ptr{runtime_storage_ptr} {
    { daemon::start(); }
}

worker &worker::current() {
    assert(_current_worker != nullptr);
    return *_current_worker;
}

worker &worker::of_handle(std::coroutine_handle<> handle) {}

void worker::awake_handle(std::coroutine_handle<> handle) noexcept {}

void worker::suspend_handle(std::coroutine_handle<> handle) noexcept {}

void worker::push_handle(std::coroutine_handle<> handle) noexcept {}

std::coroutine_handle<> worker::pop_handle() noexcept {}

bool worker::init() {
    if (m_runtime_storage_ptr != nullptr) {
        *reinterpret_cast<runtime ***>(m_runtime_storage_ptr) = &runtime::_current_runtime;
    }
    _current_worker = this;
    runtime::current().m_idle_workers.push_back(m_id);
    return true;
}

bool worker::run_once(std::stop_token &st) { return daemon::run_once(st); }

void worker::shutdown() { daemon::shutdown(); }

void worker::fetch_task() {
    if (auto handle = m_coroutine_rx.try_recv()) {
        m_backsem->release();
        m_active_stacks.push_back(std::vector{*handle});
    }
}

};  // namespace asco::core
