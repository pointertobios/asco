// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/worker.h>

#include <atomic>
#include <coroutine>
#include <format>

#include <asco/core/cancellation.h>
#include <asco/core/os/process.h>
#include <asco/core/runtime.h>
#include <asco/core/task/execution_domain.h>
#include <asco/panic.h>
#include <asco/util/tsc.h>

namespace asco::core {

worker::worker(
    std::size_t id, detail::coroutine_receiver rx,
    std::shared_ptr<std::counting_semaphore<detail::coroutine_queue_capacity + 1>> backsem,
    void *runtime_storage_ptr, void *runtime_ptr, detail::idle_workers_sender idle_tx)
        : daemon(std::format("asco-w{}", id))
        , m_execution_domain{m_scheduler}
        , m_id{id}
        , m_coroutine_rx{std::move(rx)}
        , m_backsem{std::move(backsem)}
        , m_idle_workers_tx{idle_tx}
        , m_runtime_storage_ptr{runtime_storage_ptr}
        , m_runtime_ptr{runtime_ptr} {
    { daemon::start(); }
}

worker &worker::current() {
    asco_assert(_current_worker != nullptr);
    return *_current_worker;
}

worker *worker::of_handle(std::coroutine_handle<> handle) {
    if (auto g = _corohandle_worker_map->get(handle)) {
        return g.value();
    } else {
        return nullptr;
    }
}

bool worker::handle_valid(std::coroutine_handle<> handle) { return _corohandle_worker_map->contains(handle); }

std::size_t worker::id() const { return m_id; }

void worker::register_handle(std::coroutine_handle<> handle) {
    asco_assert(handle);
    _corohandle_worker_map->insert(handle, this);
}

void worker::unregister_handle(std::coroutine_handle<> handle) {
    asco_assert(handle);
    _corohandle_worker_map->remove(handle);
}

bool worker::init() {
    if (m_runtime_storage_ptr != nullptr) {
        *reinterpret_cast<runtime ***>(m_runtime_storage_ptr) = &runtime::_current_runtime;
    }
    runtime::_current_runtime = reinterpret_cast<runtime *>(m_runtime_ptr);
    _current_worker = this;

    if (!os::thread_handle::from(daemon::m_dthread).set_affinity(os::cpu_set{}.with(m_id))) {
        panic("worker::init: 设置线程亲和性失败");
    }

    return true;
}

bool worker::run_once(std::stop_token &st) {
    if (!fetch_task() && !m_scheduler.has_active_execution()) {
        m_idle_workers_tx.try_send(m_id);
        sleep_until_awake();
        return true;
    }

    if (m_scheduler.has_active_execution()) {
        auto exit_stack = [&] {
            m_domain_stack.clear();
            m_context_stack.clear();
            m_sexec_stack.clear();
        };

        task::execution_domain *current_domain = &m_execution_domain;
        m_domain_stack.push_back(current_domain);

        do {
            auto [exec, ctx] = m_scheduler.schedule();
            m_context_stack.push_back(&ctx);
            m_sexec_stack.push_back(current_domain->schedule_execution(exec));
            current_domain = m_sexec_stack.back().get_subdomain();
            if (current_domain) {
                m_domain_stack.push_back(current_domain);
                if (!current_domain->get_scheduler().has_active_execution()) {
                    exit_stack();
                    return true;
                }
            }
        } while (current_domain);

        if (!m_executor.execute(m_sexec_stack.back(), m_context_stack)) {
            auto &domain = *m_domain_stack.back();
            auto exec = m_sexec_stack.back().m_id;
            domain.get_scheduler().detach_suspended_execution(exec);
            domain.detach_execution(exec);
        }

        exit_stack();
    }

    return !st.stop_requested() ||  //
           !m_execution_domain.is_empty() || fetch_task();
}

void worker::shutdown() {}

bool worker::fetch_task() {
    if (auto meta = m_coroutine_rx.try_recv()) {
        auto handle = meta->handle;
        meta->pdomain_location->store(&m_execution_domain, std::memory_order::release);
        m_backsem->release();
        m_coroutine_metas.insert(handle, std::move(*meta));
        m_execution_domain.attach_execution(handle, meta->cancel_source);
        m_scheduler.attach_execution(handle);
        _corohandle_worker_map->insert(handle, this);
        return true;
    }
    return false;
}

};  // namespace asco::core
