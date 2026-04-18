// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "asco/util/types.h"
#include <expected>
#include <tuple>

#include <asco/concurrency/hash_map.h>
#include <asco/core/task/execution_domain.h>
#include <asco/core/task/execution_domain_proxy.h>
#include <asco/core/task/scheduler.h>
#include <asco/future.h>
#include <asco/invoke.h>
#include <asco/sync/spinlock.h>

namespace asco::task {

class join_all_scheduler : public core::task::scheduler {
    class join_all_context final : public context {
    public:
        join_all_context(join_all_scheduler *scheduler, core::task::execution_id id)
                : m_scheduler{scheduler}
                , m_id{id} {}

        join_all_context(const join_all_context &) = delete;
        join_all_context &operator=(const join_all_context &) = delete;

        join_all_context(join_all_context &&) = default;
        join_all_context &operator=(join_all_context &&) = default;

        void end(bool completed) noexcept override {
            if ((m_scheduler->m_current_suspend && !m_scheduler->m_preawake_executions.remove(m_id))
                || completed) {
                m_scheduler->m_suspended_executions.insert(m_id);
                m_scheduler->m_domain->suspend_execution(m_id);
            } else {
                m_scheduler->m_executions.lock()->push_back(m_id);
                m_scheduler->m_domain->activate_execution(m_id);
            }
            m_scheduler->m_current_suspend = false;
        }

    private:
        join_all_scheduler *m_scheduler;
        core::task::execution_id m_id;
    };

public:
    void attach_execution(core::task::execution_id id) override {
        asco_assert(m_exec_ctx_map.insert(id, join_all_context{this, id}));
        m_executions.lock()->push_back(id);
    }

    void detach_suspended_execution(core::task::execution_id id) override {
        if (m_suspended_executions.remove(id)) {
            m_exec_ctx_map.remove(id);
        }
    }

    void awake_execution(core::task::execution_id id) noexcept override {
        if (!m_exec_ctx_map.contains(id)) {
            return;
        }

        if (m_suspended_executions.remove(id)) {
            m_executions.lock()->push_back(id);
            m_domain->activate_execution(id);
        } else {
            m_preawake_executions.insert(id);
        }

        auto parent_domain = m_domain->get_parent_domain();
        auto parent_exec = m_domain->get_parent_execution();
        if (parent_domain
            && parent_domain->get_execution_state(parent_exec) == core::task::execution_state::suspended) {
            parent_domain->get_scheduler().awake_execution(parent_exec);
        }
    }

    void suspend_current(core::task::execution_id id) noexcept override {
        asco_assert(m_current_execution == id);
        if (!m_exec_ctx_map.contains(id)) {
            return;
        }

        if (m_preawake_executions.remove(id)) {
            return;
        }

        m_domain->suspend_execution(id);
        m_current_suspend = true;
    }

    std::tuple<core::task::execution_id, context &> schedule() override {
        auto g = m_executions.lock();
        asco_assert(!g->empty());
        m_current_execution = g->front();
        g->pop_front();
        return {m_current_execution, m_exec_ctx_map.get(m_current_execution).value()};
    }

    bool has_active_execution() override { return !m_executions.lock()->empty(); }
    bool has_suspended_execution() override { return m_suspended_executions.size() || m_current_suspend; }

private:
    core::task::execution_id m_current_execution{};
    bool m_current_suspend{false};

    sync::spinlock<std::deque<core::task::execution_id>> m_executions;

    concurrency::hash_set<core::task::execution_id> m_suspended_executions;
    concurrency::hash_set<core::task::execution_id> m_preawake_executions;
    concurrency::hash_map<core::task::execution_id, join_all_context> m_exec_ctx_map;
};

template<async_function... Args>
class join_all {
    template<typename Future>
    using result_type = std::expected<
        util::types::monostate_if_void<typename std::remove_cvref_t<Future>::output_type>,
        std::exception_ptr>;

public:
    join_all(Args &&...args)
            : m_futures{std::make_tuple(co_invoke(std::forward<Args>(args))...)} {
        std::apply([&](auto &...args) { (m_domain.attach_execution(args.as_execution()), ...); }, m_futures);
    }

    bool await_ready() noexcept { return false; }

    void await_suspend(std::coroutine_handle<>) noexcept {}

    auto await_resume() {
        return std::apply(
            [](auto &...args) {
                return std::make_tuple([](auto &future) -> result_type<decltype(future)> {
                    using future_type = std::remove_cvref_t<decltype(future)>;
                    try {
                        if constexpr (future_type::output_void) {
                            future.await_resume();
                            return std::monostate{};
                        } else {
                            return future.await_resume();
                        }
                    } catch (...) { return std::unexpected{std::current_exception()}; }
                }(args)...);
            },
            m_futures);
    }

private:
    std::tuple<std::invoke_result_t<Args>...> m_futures;

    join_all_scheduler m_scheduler{};
    core::task::execution_domain_proxy m_domain{m_scheduler};
};

template<typename... Args>
join_all(Args &&...) -> join_all<std::remove_cvref_t<Args>...>;

template<util::types::move_secure T>
T fetch(std::expected<T, std::exception_ptr> &&e) {
    if (e) {
        return std::move(*e);
    } else {
        std::rethrow_exception(e.error());
    }
}

};  // namespace asco::task
