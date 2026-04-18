// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/runtime.h>

#include <memory>
#include <thread>

#include <asco/concurrency/ring_queue.h>
#include <asco/core/time/timer.h>
#include <asco/core/worker.h>
#include <asco/panic.h>

namespace asco {

bool in_runtime() noexcept { return core::runtime::_current_runtime != nullptr; }

namespace core {

runtime runtime_builder::build() && { return runtime(std::move(*this)); }

runtime::runtime(runtime_builder &&builder)
        : m_timer{std::move(builder.m_timer)} {
    auto nthreads = builder.m_nthreads;
    if (nthreads == 0) {
        nthreads = std::thread::hardware_concurrency();
        if (nthreads == 0) {
            nthreads = 1;
        }
    }

    auto [tx, rx] = detail::coroutine_queue_create();
    m_coroutine_tx = std::move(tx);
    m_backsem_sync = std::make_shared<std::counting_semaphore<detail::coroutine_queue_capacity + 1>>(
        detail::coroutine_queue_capacity);

    auto [idtx, idrx] = detail::idle_workers_create();
    m_idle_workers_rx = std::move(idrx);

    m_workers_local_runtime_ptr.assign(nthreads, nullptr);
    for (std::size_t i = 0; i < nthreads; ++i) {
        m_workers.push_back(
            std::make_unique<worker>(
                i,                                //
                rx,                               //
                m_backsem_sync,                   //
                &m_workers_local_runtime_ptr[i],  // 曾经的 runtime
                                                  // 可以被移动，这是为了支持移动留下的参数，移动现已被禁用
                this,                             //
                idtx));
        *m_workers_local_runtime_ptr[i] = this;
    }
}

runtime &runtime::current() {
    asco_assert(_current_runtime != nullptr);
    return *_current_runtime;
}

time::timer &runtime::get_timer() {
    if (!m_timer) [[unlikely]] {
        panic("runtime::get_timer: 当前 runtime 不含有 timer");
    }
    return *m_timer;
}

void runtime::awake_next() noexcept {
    if (auto id = m_idle_workers_rx.try_recv()) {
        m_workers[*id]->awake();
    }
}

};  // namespace core

};  // namespace asco
