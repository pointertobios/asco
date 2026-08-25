// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include "asco/core/scheduler.h"

#include <ranges>

#include "asco/assert.h"

namespace asco::core {

void scheduler::attach_task(const task &t) { m_queue.lock()->push_back(t); }

std::optional<task> scheduler::next_task() {
    auto &[_, rx] = m_pending_awake;
    {
        while (true) {
            auto t = rx.recv();
            if (t.has_value()) {
                awake_impl(t.value());
            } else if (t.error() != concurrency::receive_failed::pending) {
                break;
            }
        }
    }

    for (auto &slot : m_reschedule_slots) {
        if (slot.available) {
            slot.available = false;
            auto res = *slot.m_task.get();
            slot.m_task.destroy();
            m_current_task = res.m_id;
            return res;
        }
    }

    task res;
    {
        auto g = m_queue.lock();
        if (g->empty()) {
            return std::nullopt;
        }
        res = g->front();
        g->pop_front();
    }
    m_current_task = res.m_id;
    return res;
}

void scheduler::resched(task t) {
    for (auto &slot : m_reschedule_slots) {
        if (!slot.available) {
            slot.available = true;
            slot.m_task.emplace(try_move(t));
            return;
        }
    }
    m_queue.lock()->push_front(try_move(*m_reschedule_slots[0].m_task.get()));
    for (usize i{0}; i < reschedule_slots_size - 1; i++) {
        m_reschedule_slots[i].m_task.destroy();
        m_reschedule_slots[i].m_task.emplace(try_move(*m_reschedule_slots[i + 1].m_task.get()));
    }
    auto &slot = m_reschedule_slots[reschedule_slots_size - 1];
    slot.m_task.destroy();
    slot.m_task.emplace(try_move(t));
}

void scheduler::suspend(task t) {
    ASCO_ASSERT(t.m_id == m_current_task);

    if (!m_preawake.remove(t.m_id)) {
        m_suspended_tasks.insert(t.m_id, try_move(t));
    }
}

void scheduler::awake(task_id id) {
    auto &[tx, _] = m_pending_awake;
    while (!tx.send(try_move(id))) {}
}

bool scheduler::has_suspended() const { return !m_suspended_tasks.is_empty(); }

task scheduler::steal() {
    auto g = m_queue.lock();
    if (g->empty()) {
        return {};
    } else {
        auto res = g->back();
        g->pop_back();
        return res;
    }
}

void scheduler::awake_impl(task_id id) {
    if (auto t = m_suspended_tasks.remove(id)) {
        resched(*t);
    } else {
        m_preawake.insert(id);
    }
}

};  // namespace asco::core
