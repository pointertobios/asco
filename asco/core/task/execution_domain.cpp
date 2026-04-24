// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/task/execution_domain.h>

#include <algorithm>
#include <atomic>
#include <coroutine>
#include <span>
#include <utility>

#include <asco/panic.h>

namespace asco::core::task {

execution::execution(execution_id id)
        : id{id}
        , handle_stack{{id}}
        , cancel_src_stack{}
        , subdomain{nullptr} {}

execution::~execution() {}

execution::execution(execution &&rhs) noexcept
        : id{rhs.id}
        , handle_stack{std::move(rhs.handle_stack)}
        , cancel_src_stack{}
        , subdomain{rhs.subdomain}
        , cancel_src_stack_size{rhs.cancel_src_stack_size}
        , state{rhs.state.load(std::memory_order_acquire)} {
    std::ranges::copy(rhs.cancel_src_stack, cancel_src_stack);
}

execution &execution::operator=(execution &&rhs) noexcept {
    if (this != &rhs) {
        this->~execution();
        new (this) execution(std::move(rhs));
    }
    return *this;
}

void execution_domain::attach_execution(
    execution_id id, const std::span<cancel_source *> &parent_srcstack, cancel_source *cancel_src) {
    asco_assert(parent_srcstack.size() < util::compile_config::core::task::execution_domain_nest_max_depth);

    execution exec{id};
    std::ranges::copy(parent_srcstack, exec.cancel_src_stack);
    exec.cancel_src_stack[parent_srcstack.size()] = cancel_src;
    exec.cancel_src_stack[parent_srcstack.size() + 1] = nullptr;
    exec.cancel_src_stack_size = parent_srcstack.size() + 1;
    m_executions.insert(id, std::move(exec));
    m_corohandle_exec_map.insert(id, execution_id{id});
    m_execution_list.insert(id);
}

void execution_domain::detach_execution(execution_id id) {
    m_executions.remove(id);
    m_corohandle_exec_map.remove(id);
    m_execution_list.erase(id);
}

scheduled_execution execution_domain::schedule_execution(execution_id id) {
    auto g = m_executions.get(id);
    asco_assert(g);
    g.value().state.store(execution_state::running, std::memory_order::release);
    return {*this, id, &g.value()};
}

void execution_domain::suspend_execution(execution_id id) {
    auto g = m_executions.get(id);
    asco_assert(g);
    g.value().state.store(execution_state::suspended, std::memory_order::release);
}

void execution_domain::activate_execution(execution_id id) {
    auto g = m_executions.get(id);
    asco_assert(g);
    g.value().state.store(execution_state::active, std::memory_order::release);
}

void execution_domain::activate_all() {
    for (auto id : m_execution_list) {
        activate_execution(id);
    }
}

execution_state execution_domain::get_execution_state(execution_id id) {
    auto g = m_executions.get(id);
    asco_assert(g);
    return g.value().state.load(std::memory_order::acquire);
}

std::coroutine_handle<> execution_domain::top_of_execution(execution_id id) {
    if (auto g = m_executions.get(id)) {
        if (g.value().handle_stack.empty()) {
            return {};
        }
        return g.value().handle_stack.back();
    } else {
        panic("execution_domain::top_of_execution: id {{{}}} 不存在", id.address());
    }
}

execution_id execution_domain::execution_of_coroutine(std::coroutine_handle<> handle) {
    if (auto g = m_corohandle_exec_map.get(handle)) {
        return g.value();
    } else {
        panic("execution_domain::execution_of_coroutine: 协程 {{{}}} 不存在于任何执行流中", handle.address());
    }
}

};  // namespace asco::core::task
