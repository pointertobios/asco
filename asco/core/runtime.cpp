// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/runtime.h>

#include <memory>
#include <thread>

#include <asco/concurrency/ring_queue.h>
#include <asco/core/worker.h>

namespace asco {

bool in_runtime() noexcept { return core::runtime::_current_runtime != nullptr; }

namespace core {

runtime::runtime(std::size_t nthreads) {
    if (nthreads == 0) {
        nthreads = std::thread::hardware_concurrency();
        if (nthreads == 0) {
            nthreads = 1;
        }
    }

    auto [tx, rx] = detail::coroutine_queue_create();
    m_coroutine_tx = std::move(tx);
    m_backsem_sync = std::make_shared<std::counting_semaphore<detail::coroutine_queue_capacity>>(
        detail::coroutine_queue_capacity);

    m_workers_local_runtime_ptr.reserve(nthreads);
    for (std::size_t i = 0; i < nthreads; ++i) {
        m_workers.push_back(std::make_unique<worker>(i, rx, m_backsem_sync, &m_workers_local_runtime_ptr[i]));
        *m_workers_local_runtime_ptr[i] = this;
    }
}

runtime::runtime(runtime &&other)
        : m_workers{std::move(other.m_workers)}
        , m_workers_local_runtime_ptr{std::move(other.m_workers_local_runtime_ptr)} {
    for (auto &p : m_workers_local_runtime_ptr) {
        *p = this;
    }
}

runtime &runtime::operator=(runtime &&other) {
    if (this != &other) {
        m_workers = std::move(other.m_workers);
        m_workers_local_runtime_ptr = std::move(other.m_workers_local_runtime_ptr);
        for (auto &p : m_workers_local_runtime_ptr) {
            *p = this;
        }
    }
    return *this;
}

runtime &runtime::current() {
    assert(_current_runtime != nullptr);
    return *_current_runtime;
}

void runtime::awake_next() noexcept {
    auto id = m_idle_workers.front();
    m_idle_workers.pop_front();
    m_workers[id]->awake();
}

};  // namespace core

};  // namespace asco
