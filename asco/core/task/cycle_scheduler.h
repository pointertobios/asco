// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <cstddef>
#include <tuple>
#include <utility>

#include <asco/core/task/execution_domain.h>
#include <asco/core/task/scheduler.h>
#include <asco/panic.h>

namespace asco::core::task {

template<std::size_t N>
class cycle_scheduler : public scheduler {
    class cycle_context final : public context {
    public:
        cycle_context(cycle_scheduler *scheduler, std::size_t index)
                : m_scheduler{scheduler}
                , m_execution_index{index} {}

        cycle_context(const cycle_context &) = delete;
        cycle_context &operator=(const cycle_context &) = delete;

        cycle_context(cycle_context &&) = default;
        cycle_context &operator=(cycle_context &&) = default;

        void end(bool completed) noexcept override {
            auto &state = m_scheduler->m_executions[m_execution_index];
            if ((state.suspend_now && !state.preawaken) || completed) {
                state.active = false;
                m_scheduler->m_active_count--;
                m_scheduler->m_domain->suspend_execution(state.id);
            } else {
                state.preawaken = false;
                m_scheduler->m_domain->activate_execution(state.id);
            }
            state.suspend_now = false;
        }

    private:
        cycle_scheduler *m_scheduler;
        std::size_t m_execution_index;
    };

    struct execution_sched {
        execution_id id{};
        bool active{true};
        cycle_context ctx{nullptr, {}};

        bool suspend_now{false};
        bool preawaken{false};

        bool detached{false};
    };

public:
    void attach_execution(execution_id id) override {
        asco_assert(m_fill_index < N);

        m_executions[m_fill_index] =
            execution_sched{id, true, cycle_context{this, m_fill_index}, false, false};
        m_execution_index_map[m_fill_index] = {id, m_fill_index};
        ++m_active_count;
        ++m_fill_index;
    }

    void detach_suspended_execution(execution_id id) override {
        for (auto &p : m_execution_index_map) {
            if (p.first == id) {
                auto &state = m_executions[p.second];
                asco_assert(!state.active);
                state.detached = true;
                state.id = execution_id{};
                m_execution_index_map[p.second] = {execution_id{}, -1};
                m_detached_count++;
                break;
            }
        }
    }

    void awake_execution(execution_id id) noexcept override {
        for (auto &p : m_execution_index_map) {
            if (p.first == id) {
                auto &state = m_executions[p.second];
                if (state.detached) {
                    break;
                }
                if (!state.active) {
                    state.suspend_now = false;
                    state.active = true;
                    m_active_count++;
                    m_domain->activate_execution(id);
                } else {
                    state.preawaken = true;
                }
                break;
            }
        }

        auto parent_domain = m_domain->get_parent_domain();
        auto parent_exec = m_domain->get_parent_execution();
        if (parent_domain && parent_domain->get_execution_state(parent_exec) == execution_state::suspended) {
            parent_domain->get_scheduler().awake_execution(parent_exec);
        }
    }

    void suspend_current(execution_id id) noexcept override {
        for (auto &p : m_execution_index_map) {
            if (p.first == id) {
                auto &state = m_executions[p.second];
                if (state.detached) {
                    break;
                }
                if (state.preawaken) {
                    state.preawaken = false;
                    break;
                }
                state.suspend_now = true;
                m_domain->suspend_execution(id);
                break;
            }
        }
    }

    std::tuple<execution_id, context &> schedule() override {
        while (m_executions[m_current_execution_index % N].detached
               || !m_executions[m_current_execution_index % N].active) {
            m_current_execution_index++;
        }
        auto index = m_current_execution_index++ % N;
        return {m_executions[index].id, m_executions[index].ctx};
    }

    bool has_active_execution() override { return m_active_count > 0; }
    bool has_suspended_execution() override { return N - m_active_count - m_detached_count > 0; }

private:
    std::size_t m_current_execution_index{0};

    std::array<execution_sched, N> m_executions;
    std::size_t m_fill_index{0};

    std::size_t m_active_count{0};
    std::size_t m_detached_count{0};

    std::array<std::pair<execution_id, std::size_t>, N> m_execution_index_map;
};

};  // namespace asco::core::task
