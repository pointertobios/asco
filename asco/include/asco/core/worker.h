// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <format>

#include "asco/concurrency/mpsc.h"
#include "asco/core/daemon.h"
#include "asco/core/future_state.h"
#include "asco/core/predecl.h"
#include "asco/core/scheduler.h"
#include "asco/types/erased.h"

namespace asco::core {

class worker final : public daemon {
    friend class awake_token;

public:
    worker(runtime *rt, usize wid, task_receiver &&task_rx, concurrency::mpsc<usize>::sender &&acceptible_tx)
            : daemon{std::format("jg::w{}", wid)}
            , m_host_runtime{rt}
            , m_wid{wid}
            , m_task_rx{std::move(task_rx)}
            , m_acceptible_tx{std::move(acceptible_tx)} {}

    static bool exists() { return tls_this_worker; }
    static worker &current() { return *tls_this_worker; }

    bool operator==(const worker &rhs) const { return m_wid == rhs.m_wid; }

    usize id() const { return m_wid; }

    task_id this_task_id() const { return m_this_task; }

    runtime &host_runtime() const { return *m_host_runtime; }

    scheduler &get_scheduler() { return m_scheduler; }

    bool fetch_task();

    void set_next_resume(std::coroutine_handle<> coroutine);
    void set_suspend_now();
    void set_yield_now();

private:
    bool initialize() override;
    bool run_once(std::stop_token &st) override;
    void finalize() override {}

    runtime *const m_host_runtime;
    const usize m_wid;
    task_receiver m_task_rx;
    concurrency::mpsc<usize>::sender m_acceptible_tx;

    scheduler m_scheduler;
    /* 当前状态 */
    task_id m_this_task{};
    std::coroutine_handle<> m_next_resume{};
    bool m_suspend_now{false};
    bool m_yield_now{false};

    inline thread_local static worker *tls_this_worker{nullptr};
};

class awake_token {
public:
    awake_token() { ASCO_ASSERT(worker::exists()); }

    ~awake_token() = default;

    awake_token(const awake_token &) = default;
    awake_token &operator=(const awake_token &) = default;

    awake_token(awake_token &&) = default;
    awake_token &operator=(awake_token &&) = default;

    static awake_token none() { return awake_token{nullptr, task_id{}}; }

    operator bool() const { return m_worker; }

    bool operator==(const awake_token &rhs) const { return m_worker == rhs.m_worker && m_task == rhs.m_task; }

    void suspend(std::coroutine_handle<> resume_coroutine);

    void awake();

private:
    awake_token(worker *w, task_id t)
            : m_worker{w}
            , m_task{t} {}

    worker *m_worker{&worker::current()};
    task_id m_task{m_worker->m_this_task};
};

struct task_block_base {
    std::atomic<future_state> m_state;
    awake_token m_awake_token{awake_token::none()};

    std::binary_semaphore m_sync_awaiter{0};

    std::vector<task_block_base *> m_subtasks{};

    types::erased m_bound_invocable{};

    task_id to_task_id() const { return reinterpret_cast<void *>(const_cast<task_block_base *>(this)); }

    static task_block_base *from_task_id(task_id id) { return reinterpret_cast<task_block_base *>(id); }
};

};  // namespace asco::core
