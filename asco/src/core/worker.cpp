// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include "asco/core/worker.h"

#include <coroutine>
#include <random>

#include "asco/util/rng.h"

namespace asco::core {

bool worker::fetch_task() {
    std::optional<task_item> new_task;
    usize c = 0;

    while ((new_task = m_task_rx.recv()).has_value()) {
        auto &[root_coroutine, task_block] = new_task.value();
        m_scheduler.attach_task(task{task_block->to_task_id(), root_coroutine});
        c++;
    }

    if (c != 0) {
        return true;
    }

    runtime &rt = *m_host_runtime;
    usize worker_count = rt.worker_count();
    thread_local std::uniform_int_distribution<usize> w{0, worker_count - 1};
    usize steal_wid = w(util::rng());
    if (steal_wid == m_wid) {
        steal_wid = (steal_wid + 1) % worker_count;
    }
    auto &steal_worker = rt.get_worker(steal_wid);
    task t = steal_worker.get_scheduler().steal();
    if (t) {
        m_scheduler.attach_task(t);
        c++;
    }

    return c != 0;
}

void worker::set_next_resume(std::coroutine_handle<> coroutine) { m_next_resume = coroutine; }

void worker::set_suspend_now() { m_suspend_now = true; }

void worker::set_yield_now() { m_yield_now = true; }

bool worker::initialize() {
    tls_this_worker = this;

    if (!os::thread_handle::this_thread().set_affinity(os::cpu_set().with(m_wid))) {
        panic("Failed to set affinity for worker thread {}", m_wid);
    }
    return true;
}

bool worker::run_once(std::stop_token &st) {
    while (!m_acceptible_tx.send(m_wid)) {}

    bool fetched_new_task = fetch_task();
    if (!fetched_new_task) {
        wait_for_awake();
    }

    std::optional<task> t{std::nullopt};
    while ((t = m_scheduler.next_task())) {
        m_this_task = t->m_id;
        while (!m_suspend_now && !m_yield_now) {
            t->m_resume_handle.resume();
        }
        if (m_next_resume) {
            t->m_resume_handle = m_next_resume;
            m_next_resume = std::coroutine_handle<>{};
            if (m_suspend_now) {
                m_scheduler.suspend(*t);
            } else {
                m_scheduler.resched(*t);
            }
        }
        m_suspend_now = false;
        m_yield_now = false;
    }

    return t || fetched_new_task || m_scheduler.has_suspended() || !st.stop_requested();
}

void awake_token::suspend(std::coroutine_handle<> resume_coroutine) {
    ASCO_ASSERT(*m_worker == worker::current());

    m_worker->set_suspend_now();
    m_worker->set_next_resume(resume_coroutine);
}

void awake_token::awake() {
    m_worker->get_scheduler().awake(m_task);
    m_worker->awake();
}

};  // namespace asco::core
