// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_FUTURES_H
#define ASCO_FUTURES_H 1

#include <coroutine>

#include <asco/core/taskgroup.h>
#include <asco/coro_local.h>
#include <asco/future.h>
#include <asco/rterror.h>
#include <asco/utils/type_hash.h>

namespace asco::base::this_coro {

using core::is_runtime;

template<typename R = RT>
    requires is_runtime<R>
bool aborted() {
    return RT::__worker::get_worker().current_task().aborted;
}

template<typename T>
struct aborted_value_t {
    std::byte null[sizeof(T)];
};

template<typename T>
inline static auto aborted_value_v = aborted_value_t<T>{};

template<typename T>
inline static T aborted_value = std::move(*(T *)(&aborted_value_v<T>.null));

template<typename F, typename T, typename R = RT>
    requires is_future<F> && is_runtime<R>
T &&move_back_return_value() {
    auto h_ = RT::__worker::get_worker().current_task().handle;
    typename F::corohandle h = *(typename F::corohandle *)(&h_);
    if (h.promise().future_type_hash != type_hash<F>())
        throw asco::runtime_error(
            "[ASCO] move_back_return_value<F, T>(): F is not matched with your current coroutine.");
    if (std::is_same_v<typename F::return_type, T>)
        throw asco::runtime_error(
            "[ASCO] move_back_return_value<F, T>(): T is not matched with your current coroutine.");
    // If a task aborted and must move back return value, its awaiter will always exists.
    return h.promise().awaiter->retval_move_out();
}

template<typename R = RT>
    requires is_runtime<R>
size_t get_id() {
    return RT::__worker::get_worker().current_task_id();
}

template<typename R = RT>
    requires is_runtime<R>
RT::__worker &get_worker() {
    return RT::__worker::get_worker();
}

template<size_t Hash, typename R = RT>
    requires is_runtime<R>
bool coro_local_exists() {
    return RT::__worker::get_worker().current_task().coro_local_frame->var_exists<Hash>();
}

namespace inner {

template<size_t Hash, typename R = RT>
    requires is_runtime<R>
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

using base::this_coro::aborted_value;

};  // namespace asco::this_coro

#endif
