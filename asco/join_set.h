// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <asco/future.h>
#include <asco/generator.h>
#include <asco/invoke.h>
#include <asco/sync/channel.h>
#include <asco/utils/concepts.h>

namespace asco::base {

using namespace concepts;

namespace cq = continuous_queue;

template<move_secure T = void, queue::creator<T> Creator = decltype(cq::create<T>)>
class join_set {
public:
    explicit join_set(Creator ctor) { std::tie(tx, rx) = channel<T>(ctor); }

    explicit join_set()
            : join_set(cq::create<T>) {}

    join_set(const join_set &) noexcept = delete;
    join_set &operator=(const join_set &) noexcept = delete;

    join_set(join_set &&) noexcept = delete;
    join_set &operator=(join_set &&) noexcept = delete;

    // Just call but do not co_await
    template<async_function Fn>
        requires std::is_same_v<typename std::invoke_result_t<Fn>::deliver_type, T>
    future_spawn<void> spawn(Fn fn) {
        active_count.fetch_add(1, std::memory_order_acq_rel);
        if constexpr (std::is_same_v<std::invoke_result_t<std::remove_cvref_t<Fn>>, future<T>>) {
            co_await tx.send(co_await co_invoke(std::move(fn)).spawn());
        } else {
            co_await tx.send(co_await co_invoke(std::move(fn)));
        }
    }

    generator<T> join() {
        for (; active_count.load(morder::acquire) > 0; active_count.fetch_sub(1, morder::acq_rel)) {
            if (auto res = co_await rx.recv(); res) {
                co_yield std::move(*res);
            } else {
                break;
            }
        }
    }

private:
    atomic_size_t active_count{0};
    sender<T, typename Creator::Sender> tx;
    receiver<T, typename Creator::Receiver> rx;
};

};  // namespace asco::base

namespace asco {

using base::join_set;

};  // namespace asco
