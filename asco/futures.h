// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_FUTURES_H
#define ASCO_FUTURES_H 1

#include <asco/core/taskgroup.h>
#include <asco/coro_local.h>
#include <asco/future.h>
#include <asco/rterror.h>
#include <asco/utils/type_hash.h>

namespace asco::base::this_coro {

using core::runtime_type;

template<runtime_type R = RT>
bool aborted() {
    return RT::__worker::get_worker().current_task().aborted;
}

template<future_type F, runtime_type R = RT>
F::return_type move_back_return_value() {
    auto h_ = RT::__worker::get_worker().current_task().handle;
    typename F::corohandle h = *(typename F::corohandle *)(&h_);
    if (h.promise().future_type_hash != type_hash<F>())
        throw asco::runtime_error(
            "[ASCO] move_back_return_value<F, T>(): F is not matched with your current coroutine.");
    // If a task aborted and must move back return value, its awaiter will always exists.
    return h.promise().awaiter->retval_move_out();
}

template<future_type F, runtime_type R = RT>
void throw_coroutine_abort() {
    auto h_ = RT::__worker::get_worker().current_task().handle;
    typename F::corohandle h = *(typename F::corohandle *)(&h_);
    if (h.promise().future_type_hash != type_hash<F>())
        throw asco::runtime_error(
            "[ASCO] move_back_return_value<F, T>(): F is not matched with your current coroutine.");
    h.promise().awaiter->set_abort_exception();
}

template<runtime_type R = RT>
size_t get_id() {
    return RT::__worker::get_worker().current_task_id();
}

template<runtime_type R = RT>
RT::__worker &get_worker() {
    return RT::__worker::get_worker();
}

template<size_t Hash, runtime_type R = RT>
bool coro_local_exists() {
    return RT::__worker::get_worker().current_task().coro_local_frame->var_exists<Hash>();
}

namespace inner {

template<size_t Hash, runtime_type R = RT>
bool group_local_exists() {
    return RT::get_runtime().group(RT::__worker::get_worker().current_task_id())->var_exists<Hash>();
}

// Do **NOT** let all the cloned coroutines co_return!!!!!
size_t clone(std::coroutine_handle<> h);

};  // namespace inner

};  // namespace asco::base::this_coro

namespace asco::this_coro {

using base::this_coro::aborted, base::this_coro::move_back_return_value;
using base::this_coro::coro_local_exists;
using base::this_coro::get_id;
using base::this_coro::get_worker;
using base::this_coro::throw_coroutine_abort;

};  // namespace asco::this_coro

#endif
