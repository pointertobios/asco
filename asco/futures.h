// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_FUTURES_H
#define ASCO_FUTURES_H 1

#include <concepts>
#include <coroutine>

#include <asco/core/taskgroup.h>
#include <asco/coro_local.h>
#include <asco/future.h>
#include <asco/utils/type_hash.h>

namespace asco::base::futures {

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
        throw std::runtime_error(
            "[ASCO] move_back_return_value<F, T>(): F is not matched with your current coroutine.");
    if (std::is_same_v<typename F::return_type, T>)
        throw std::runtime_error(
            "[ASCO] move_back_return_value<F, T>(): T is not matched with your current coroutine.");
    return std::move(h.promise().retval);
}

template<typename R = RT>
    requires is_runtime<R>
size_t get_task_id() {
    return RT::__worker::get_worker().current_task_id();
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

};  // namespace asco::base::futures

namespace asco::futures {

using base::futures::aborted, base::futures::move_back_return_value;
using base::futures::coro_local_exists;
using base::futures::get_task_id;

using base::futures::aborted_value;

namespace inner {

using base::futures::inner::clone;
using base::futures::inner::group_local_exists;

};  // namespace inner

};  // namespace asco::futures

#endif
