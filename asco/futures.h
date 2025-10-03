// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_FUTURES_H
#define ASCO_FUTURES_H 1

#include <asco/compile_time/string.h>
#include <asco/core/taskgroup.h>
#include <asco/coro_local.h>
#include <asco/future.h>
#include <asco/rterror.h>
#include <asco/utils/type_hash.h>

namespace asco::base::this_coro {

constexpr bool aborted() { return RT::__worker::get_worker().current_task().aborted; }

template<typename F>
F::return_type move_back_return_value() {
    auto h_ = RT::__worker::get_worker().current_task().handle;
    auto h = *(std::coroutine_handle<typename F::promise_type> *)(&h_);
    if (h.promise().future_type_hash != inner::type_hash<F>())
        throw asco::runtime_error(
            "[ASCO] move_back_return_value<F>(): F is not matched with your current coroutine.");
    // If a task aborted and must move back return value, its awaiter will always exists.
    return h.promise().retval_move_out();
}

template<typename F>
std::vector<typename F::return_type> move_back_generated_values() {
    auto h_ = RT::__worker::get_worker().current_task().handle;
    auto h = *(std::coroutine_handle<typename F::promise_type> *)(&h_);
    if (h.promise().future_type_hash != inner::type_hash<F>())
        throw asco::runtime_error(
            "[ASCO] move_back_generated_values<F>(): F is not matched with your current coroutine.");
    return h.promise().genvals_move_out();
}

template<typename F>
void throw_coroutine_abort() {
    auto h_ = RT::__worker::get_worker().current_task().handle;
    auto h = *(std::coroutine_handle<typename F::promise_type> *)(&h_);
    if (h.promise().future_type_hash != inner::type_hash<F>())
        throw asco::runtime_error(
            "[ASCO] move_back_return_value<F>(): F is not matched with your current coroutine.");
    h.promise().set_abort_exception();
}

constexpr size_t get_id() { return RT::__worker::get_worker().current_task_id(); }

constexpr RT::__worker &get_worker() { return RT::__worker::get_worker(); }

template<compile_time::string Name>
bool coro_local_exists() {
    return RT::__worker::get_worker().current_task().coro_local_frame->var_exists<Name>();
}

namespace inner {

template<compile_time::string Name>
bool group_local_exists() {
    return RT::get_runtime().group(RT::__worker::get_worker().current_task_id())->var_exists<Name>();
}

// Do **NOT** let all the cloned coroutines co_return!!!!!
size_t clone(std::coroutine_handle<> h);

};  // namespace inner

};  // namespace asco::base::this_coro

namespace asco::this_coro {

using base::this_coro::aborted;
using base::this_coro::coro_local_exists;
using base::this_coro::get_id;
using base::this_coro::get_worker;
using base::this_coro::move_back_return_value, base::this_coro::move_back_generated_values;
using base::this_coro::throw_coroutine_abort;

};  // namespace asco::this_coro

#endif
