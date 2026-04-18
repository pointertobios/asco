// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/worker.h>

#include <atomic>
#include <coroutine>
#include <format>
#include <ranges>

#include <asco/core/cancellation.h>
#include <asco/core/os/process.h>
#include <asco/core/runtime.h>
#include <asco/core/task/execution_domain.h>
#include <asco/panic.h>

namespace asco::core {

worker::worker(
    std::size_t id, detail::coroutine_receiver rx,
    std::shared_ptr<std::counting_semaphore<detail::coroutine_queue_capacity + 1>> backsem,
    void *runtime_storage_ptr, void *runtime_ptr, detail::idle_workers_sender idle_tx)
        : daemon(std::format("asco::w{}", id))
        , m_execution_domain{m_scheduler}
        , m_id{id}
        , m_coroutine_rx{std::move(rx)}
        , m_backsem{std::move(backsem)}
        , m_idle_workers_tx{idle_tx}
        , m_runtime_storage_ptr{runtime_storage_ptr}
        , m_runtime_ptr{runtime_ptr} {
    m_scheduler.bind_execution_domain(m_execution_domain);
    auto _ = daemon::start();
}

worker &worker::current() {
    asco_assert(_current_worker != nullptr);
    return *_current_worker;
}

std::size_t worker::id() const { return m_id; }

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
        auto exit_stack = [&](bool executed = false) {
            if (!executed) {
                std::ranges::for_each(m_context_stack, [](auto &ctx) { ctx->begin(); });
                std::ranges::for_each(
                    m_context_stack | std::views::reverse, [](auto &ctx) { ctx->end(false); });
            }
            m_domain_stack.clear();
            m_context_stack.clear();
            m_sexec_stack.clear();
        };

        task::execution_domain *current_domain = &m_execution_domain;
        m_domain_stack.push_back(current_domain);

        do {
            auto &current_scheduler = current_domain->get_scheduler();
            auto [exec, ctx] = current_scheduler.schedule();
            m_context_stack.push_back(&ctx);
            m_sexec_stack.push_back(current_domain->schedule_execution(exec));
            current_domain = m_sexec_stack.back().get_subdomain();
            if (current_domain) {
                if (current_domain->is_empty()) {  // 空子执行域协议：如果子执行域没有附加任何 execution
                                                   // 视为子执行域已经完成， 自动从父 execution 移除
                    m_sexec_stack.back().m_exec->remove_subdomain();
                    exit_stack();
                    return true;
                } else {
                    m_domain_stack.push_back(current_domain);
                }
                if (!current_domain->get_scheduler().has_active_execution()) {
                    // 适应性挂起，对应调度器的唤醒应有唤醒传递，将当前 execution 的唤醒向所在
                    // execution_domain 的父 execution 传递
                    current_domain->get_parent_domain()->get_scheduler().suspend_current(
                        m_sexec_stack.back().m_id);
                    // suspend_current 内部设置挂起标志，随后 exit_stack 中的 ctx->end(false) 会挂起当前
                    // execution 这与普通的协程挂起是一致的
                    exit_stack();
                    goto executed_once;
                }
            }
        } while (current_domain);

        if (!m_executor.execute(m_sexec_stack.back(), m_context_stack)) {
            auto &domain = *m_domain_stack.back();
            auto exec = m_sexec_stack.back().m_id;
            domain.get_scheduler().detach_suspended_execution(exec);
            domain.detach_execution(exec);
        }

        exit_stack(true);
    }
executed_once:

    return !st.stop_requested() ||  //
           !m_execution_domain.is_empty() || fetch_task();
}

void worker::shutdown() {}

bool worker::fetch_task() {
    if (auto meta = m_coroutine_rx.try_recv()) {
        m_backsem->release();
        auto handle = meta->handle;
        new (meta->pcancel_awake_token_storage->get()) awake_token{this, &m_execution_domain, meta->handle};
        meta->pcancel_awake_token_location->store(
            meta->pcancel_awake_token_storage->get(), std::memory_order::release);
        m_execution_domain.attach_execution(handle, meta->cancel_source);
        m_coroutine_metas.insert(handle, std::move(*meta));
        m_scheduler.attach_execution(handle);
        return true;
    }
    return false;
}

awake_token::awake_token()
        : m_worker{&worker::current()}
        , m_domain{&m_worker->get_current_execution_domain()}
        , m_exec{m_worker->get_executor().current_execution()} {}

void awake_token::suspend() noexcept { m_domain->get_scheduler().suspend_current(m_exec); }

void awake_token::awake() noexcept {
    m_domain->get_scheduler().awake_execution(m_exec);
    m_worker->awake();
}

std::size_t awake_token::hash() const noexcept {
    std::size_t h1 = std::hash<worker *>{}(m_worker);
    std::size_t h2 = std::hash<task::execution_domain *>{}(m_domain);
    std::size_t h3 = std::hash<task::execution_id>{}(m_exec);
    return h1 ^ (h2 << 1) ^ (h3 << 2);
}

};  // namespace asco::core
