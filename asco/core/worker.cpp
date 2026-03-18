// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include "os/process.h"
#include <algorithm>
#include <asco/core/worker.h>

#include <coroutine>
#include <format>

#include <asco/core/cancellation.h>
#include <asco/core/runtime.h>
#include <asco/panic.h>
#include <asco/util/tsc.h>

namespace asco::core {

worker::worker(
    std::size_t id, detail::coroutine_receiver rx,
    std::shared_ptr<std::counting_semaphore<detail::coroutine_queue_capacity + 1>> backsem,
    void *runtime_storage_ptr, void *runtime_ptr, detail::idle_workers_sender idle_tx)
        : daemon(std::format("asco-w{}", id))
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

worker &worker::of_handle(std::coroutine_handle<> handle) {
    if (auto g = _corohandle_worker_map->get(handle)) {
        return *g.value();
    } else {
        panic("worker::of_handle: 无效的 coroutine_handle {{{}}}", handle.address());
    }
}

worker *worker::optional_of_handle(std::coroutine_handle<> handle) {
    if (auto g = _corohandle_worker_map->get(handle)) {
        return g.value();
    } else {
        return nullptr;
    }
}

bool worker::handle_valid(std::coroutine_handle<> handle) { return _corohandle_worker_map->contains(handle); }

std::size_t worker::id() const { return m_id; }

std::coroutine_handle<> worker::this_coroutine() const {
    if (m_current_task.size()) {
        return m_current_task.back();
    } else {
        panic("worker::this_coroutine: 当前 worker 没有正在运行的协程");
    }
}

void worker::awake_handle(std::coroutine_handle<> handle) noexcept {
    if (auto task = m_suspended_tasks.remove(handle)) {
        if (auto g = m_active_tasks.lock()) {
            g->push_back(std::move(*task));
            std::push_heap(g->begin(), g->end());
        }
        awake();
    } else {
        m_preawake_handles.insert(handle);
    }
}

void worker::suspend_current_handle(std::coroutine_handle<> handle) noexcept {
    if (m_current_task.size() == 0 || m_current_task.back() != handle) {
        panic(
            "worker::suspend_current_handle: 挂起当前协程错误；挂起：{{{}}}，当前：{{{}}}", handle.address(),
            m_current_task.size() ? m_current_task.back().address() : nullptr);
    }
    if (m_preawake_handles.remove(handle)) {
        return;
    }
    m_suspended_tasks.insert(handle, detail::task{m_current_task_time, std::move(m_current_task)});
}

void worker::push_handle(std::coroutine_handle<> handle) noexcept {
    _corohandle_worker_map->insert(handle, this);
    m_current_task.push_back(handle);
    m_top_of_join_handle.get(m_current_task.front()).value() = handle;
}

std::coroutine_handle<> worker::pop_handle() noexcept {
    auto res = m_current_task.back();
    if (m_current_task.size() == 1) {
        m_coroutine_metas.remove(m_current_task.back());
    }
    m_current_task.pop_back();
    if (m_current_task.size()) {
        m_top_of_join_handle.get(m_current_task.front()).value() = m_current_task.back();
    } else {
        m_top_of_join_handle.remove(res);
    }
    _corohandle_worker_map->remove(res);
    return res;
}

std::coroutine_handle<> worker::top_of_join_handle(std::coroutine_handle<> handle) noexcept {
    if (auto g = m_top_of_join_handle.get(handle)) {
        return g.value();
    } else {
        return {};
    }
}

void worker::close_cancellation(std::coroutine_handle<> handle) noexcept {
    if (m_current_task.size() && m_current_task.front() == handle) {
        m_current_cancel_token.close_cancellation();
    }
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
    if (!fetch_task() && m_active_tasks.lock()->empty()) {
        m_idle_workers_tx.try_send(m_id);
        sleep_until_awake();
        return true;
    }

    auto put_current_task = [&](decltype(m_active_tasks)::guard &&g) {
        auto p = g->begin();
        m_current_task = std::move(p->stack);
        m_current_task_time = p->prio;
        std::pop_heap(g->begin(), g->end());
        g->pop_back();

        m_current_cancel_token =
            m_coroutine_metas.get(m_current_task.front()).value().cancel_source->get_token();
    };

    auto clean_empty_tasks = [&](decltype(m_active_tasks)::guard &&g) {
        while (!g->empty() && g->begin()->stack.empty()) {
            std::pop_heap(g->begin(), g->end());
            g->pop_back();
        }
    };

    clean_empty_tasks(m_active_tasks.lock());
    if (auto g = m_active_tasks.lock(); !g->empty()) {
        put_current_task(std::move(g));
    }

    while (m_current_task.size()) {
        if (cancel_cleanup()) {
            break;
        }

        m_start_tsc = util::get_tsc();

        m_current_task.back().resume();

        if (m_current_task.size() == 0) {
            // 要么是异步任务已经完成，要么是任务主动挂起或 yield，都不需要进行后续处理
            break;
        }

        if (cancel_cleanup()) {
            break;
        }

        auto dur = util::get_tsc() - m_start_tsc;
        m_current_task_time += dur;

        clean_empty_tasks(m_active_tasks.lock());

        if (auto g = m_active_tasks.lock(); !g->empty() && g->begin()->prio < m_current_task_time) {
            g->push_back({m_current_task_time, std::move(m_current_task)});
            std::push_heap(g->begin(), g->end());

            put_current_task(std::move(g));
        }
    }

    m_current_cancel_token = cancel_token{};

    return !st.stop_requested() ||  //
           m_active_tasks.lock()->size() || m_suspended_tasks.size() || fetch_task();
}

void worker::shutdown() {}

bool worker::fetch_task() {
    if (auto meta = m_coroutine_rx.try_recv()) {
        auto handle = meta->handle;
        m_backsem->release();
        m_coroutine_metas.insert(handle, std::move(*meta));
        m_top_of_join_handle.insert(handle, std::coroutine_handle{handle});
        _corohandle_worker_map->insert(handle, this);

        if (auto g = m_active_tasks.lock()) {
            g->push_back({0, std::vector{handle}});
            std::push_heap(g->begin(), g->end());
        }

        return true;
    }
    return false;
}

bool worker::cancel_cleanup() noexcept {
    if (!m_current_task.size() ||  //
        !m_current_cancel_token || !m_current_cancel_token.cancel_requested()) {
        return false;
    }

    if (m_current_cancel_token.cancellation_closed()) {
        panic("asco::worker: 外部取消了已被关闭取消的任务 {{{}}}", m_current_task.front().address());
    }

    auto s = m_current_cancel_token.source();
    s->invoke_callbacks();
    while (m_current_task.size()) {
        auto h = pop_handle();
        h.destroy();
    }
    return true;
}

void worker::yield_current() {
    if (m_current_task.size() == 0) {
        return;
    }

    auto stack_time = m_current_task_time + util::get_tsc() - m_start_tsc;
    if (auto g = m_active_tasks.lock()) {
        g->push_back({stack_time, std::move(m_current_task)});
        std::push_heap(g->begin(), g->end());
    }
}

};  // namespace asco::core
