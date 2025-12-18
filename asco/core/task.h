// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <coroutine>
#include <exception>
#include <memory>
#include <optional>
#include <semaphore>
#include <stack>

#include <asco/concurrency/concurrency.h>
#include <asco/panic/panic.h>
#include <asco/panic/unwind.h>
#include <asco/sync/rwspin.h>
#include <asco/utils/concepts.h>
#include <asco/utils/erased.h>
#include <asco/utils/memory_slot.h>
#include <asco/utils/types.h>

#include <cpptrace/basic.hpp>

namespace asco::core {

using namespace types;
using namespace concepts;

using concurrency::atomic_ptr;

class worker;

using task_id = size_t;

template<
    move_secure T = type_erase, bool Spawn = true, bool Core = true, bool UseReturnValue = true,
    typename StateExtra = void, typename = void>
struct task;

template<>
struct task<> {
    const task_id id;
    const std::coroutine_handle<> corohandle;
    const bool spawn_task;
    const bool core_task;

    std::optional<panic::coroutine_trace_handle> caller_coroutine_trace;

    atomic_bool await_started{false};
    atomic_bool returned{false};

    atomic_bool cancelled{false};

    atomic_bool e_thrown{false};
    atomic_bool e_rethrown{false};

    atomic_bool scheduled{false};

    std::exception_ptr e_ptr{};  // Released by `e_thrown`

    std::shared_ptr<task<>> caller;  // Released by `await_started`

    worker *worker_ptr{nullptr};  // Released by `scheduled`

    std::optional<std::binary_semaphore> wait_sem{std::nullopt};
    rwspin<> sync_waiting_lock;

    cpptrace::stacktrace raw_stacktrace;

    std::stack<task_id> call_chain;

    task(
        task_id id, std::coroutine_handle<> corohdl, bool spawn_task, bool core_task,
        panic::coroutine_trace_handle caller_coroutine_addr) noexcept
            : id{id}
            , corohandle{corohdl}
            , spawn_task{spawn_task}
            , core_task{core_task}
            , caller_coroutine_trace{caller_coroutine_addr} {
        call_chain.push(id);
    }

    virtual ~task() noexcept = default;

protected:
    void destroy() noexcept {
        if (e_thrown.load(morder::acquire) && !e_rethrown.load(morder::acquire)) {
            try {
                std::rethrow_exception(e_ptr);
            } catch (const std::exception &e) {
                panic::panic(
                    std::format(
                        "Exception thrown by task {} hasn't been caught:\n  what(): {}", id, e.what()));
            } catch (...) {
                panic::panic(std::format("Unknown exception thrown by task {} hasn't been caught.", id));
            }
        }
    }
};

template<move_secure T, bool Spawn, bool Core, bool UseReturnValue, typename StateExtra>
struct task<T, Spawn, Core, UseReturnValue, StateExtra> : public task<> {
    using deliver_type = T;
    constexpr static bool deliver_type_is_void = std::is_void_v<deliver_type>;

    memory_slot<std::conditional_t<UseReturnValue, deliver_type, void>>
        delivery_slot;  // Release by `returned`

    memory_slot<StateExtra> extra_slot;

    task(
        task_id id, std::coroutine_handle<> corohdl,
        panic::coroutine_trace_handle caller_coroutine_addr) noexcept
            : task<>{id, corohdl, Spawn, Core, caller_coroutine_addr} {}

    ~task() noexcept override { destroy(); }

    void bind_lambda(std::unique_ptr<utils::erased> &&f) { bound_lambda = std::move(f); }

private:
    std::unique_ptr<utils::erased> bound_lambda{nullptr};
};

};  // namespace asco::core
