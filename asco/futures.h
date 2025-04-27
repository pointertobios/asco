// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_FUTURES_H
#define ASCO_FUTURES_H 1

#include <concepts>

#include <asco/future.h>

namespace asco::futures {

template<typename F, typename R = RT>
    requires is_future<F> && is_runtime<R>
bool aborted() {
    auto h_ = RT::__worker::get_worker()->current_task().handle;
    typename F::corohandle h = *(typename F::corohandle *)(&h_);
    if (h.promise().future_type_hash != type_hash<F>())
        throw std::runtime_error("[ASCO] aborted<F>(): F is not matched with your current coroutine.");
    return h.promise().aborted.load(morder::acquire);
}

template<typename F, typename T, typename R = RT>
    requires is_future<F> && is_runtime<R>
T &&move_back_return_value() {
    auto h_ = RT::__worker::get_worker()->current_task().handle;
    typename F::corohandle h = *(typename F::corohandle *)(&h_);
    if (h.promise().future_type_hash != type_hash<F>())
        throw std::runtime_error(
            "[ASCO] move_back_return_value<F, T>(): F is not matched with your current coroutine.");
    if (std::is_same_v<typename F::return_type, T>)
        throw std::runtime_error(
            "[ASCO] move_back_return_value<F, T>(): T is not matched with your current coroutine.");
    return std::move(h.promise().awaiter->retval);
}

template<typename R = RT>
    requires is_runtime<R>
size_t get_task_id() {
    return RT::__worker::get_worker()->current_task_id();
}

template<typename F, typename R = RT>
    requires is_future<F> && is_runtime<R>
size_t get_caller_id() {
    auto h_ = RT::__worker::get_worker()->current_task().handle;
    typename F::corohandle h = *(typename F::corohandle *)(&h_);
    if (h.promise().future_type_hash != type_hash<F>())
        throw std::runtime_error("[ASCO] aborted<F>(): F is not matched with your current coroutine.");
    return h.promise().caller_task_id;
}

};  // namespace asco::futures

#endif
