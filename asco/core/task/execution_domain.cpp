// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/task/execution_domain.h>

#include <coroutine>

#include <asco/panic.h>

namespace asco::core::task {

execution::execution(execution_id id, cancel_source *src)
        : handle_stack{{id}}
        , cancel_src{src}
        , subdomain{nullptr}
        , cancel_src_owned{false} {
    if (!src) {
        new (cancel_src_storage.get()) cancel_source{};
        cancel_src = cancel_src_storage.get();
        cancel_src_owned = true;
    }
}

execution::~execution() {
    if (cancel_src_owned) {
        cancel_src_storage.get()->~cancel_source();
    }
}

execution::execution(execution &&rhs) noexcept
        : handle_stack{std::move(rhs.handle_stack)}
        , cancel_src{rhs.cancel_src}
        , subdomain{rhs.subdomain}
        , cancel_src_owned{rhs.cancel_src_owned} {
    rhs.cancel_src = nullptr;
    rhs.cancel_src_owned = false;
    rhs.subdomain = nullptr;
    if (cancel_src_owned) {
        rhs.cancel_src_storage.get()->~cancel_source();
        cancel_src = new (cancel_src_storage.get()) cancel_source{};
    }
}

execution &execution::operator=(execution &&rhs) noexcept {
    if (this != &rhs) {
        this->~execution();
        new (this) execution(std::move(rhs));
    }
    return *this;
}

void execution_domain::attach_execution(execution_id id) {
    m_executions.insert(id, execution{id, nullptr});
    m_corohandle_exec_map.insert(id, execution_id{id});
}

void execution_domain::attach_execution(execution_id id, cancel_source *cancel_src) {
    m_executions.insert(id, execution{id, cancel_src});
    m_corohandle_exec_map.insert(id, execution_id{id});
}

void execution_domain::detach_execution(execution_id id) {
    m_executions.remove(id);
    m_corohandle_exec_map.remove(id);
}

scheduled_execution execution_domain::schedule_execution(execution_id id) {
    auto g = m_executions.get(id);
    asco_assert(g);
    return {*this, id, &g.value()};
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
