// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include "asco/utils/types.h"
#include <array>
#include <exception>
#include <limits>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <asco/context.h>
#include <asco/core/task.h>
#include <asco/future.h>
#include <asco/invoke.h>
#include <asco/panic/panic.h>
#include <asco/sync/semaphore.h>
#include <asco/utils/memory_slot.h>
#include <asco/yield.h>

namespace asco::selects {

using namespace concepts;
using namespace types;

constexpr size_t select_index_null = std::numeric_limits<size_t>::max();

template<size_t I, typename T>
struct branch {
    constexpr static size_t branch_index = I;

    branch(T &&v) noexcept
            : val{std::forward<T>(v)} {}

    T &operator*() noexcept { return val; }
    const T &operator*() const noexcept { return val; }

    T *operator->() noexcept { return &val; }
    const T *operator->() const noexcept { return &val; }

private:
    T val;
};

struct select_base {
    binary_semaphore barrier{1};
    binary_semaphore waiter{0};
    size_t index{select_index_null};  // Released by waiter
    std::vector<notify *> notify_list;
    atomic_size_t select_tasks{0};
};

template<typename Future>
    requires future_type<Future> || cancellable_waitable<Future>
auto select_task(
    const size_t index, std::shared_ptr<select_base> base, std::shared_ptr<context> ctx, Future f)
    -> future_spawn<typename std::remove_cvref_t<Future>::element_type::deliver_type> {
    using ReturnType = typename std::remove_cvref_t<Future>::element_type::deliver_type;
    constexpr static bool is_cancellable_awaitable = cancellable_waitable<Future>;

    base->select_tasks.fetch_add(1, morder::acq_rel);
    base->waiter.try_acquire();
    struct lazy {
        std::shared_ptr<select_base> &base;
        bool won{false};
        ~lazy() {
            if (base->select_tasks.fetch_sub(1, morder::acq_rel) == 1 && !won) {
                base->waiter.release();
            }
        }
    } l{base};
    (void)l;

    utils::memory_slot<ReturnType> result_slot;

    std::optional<std::exception_ptr> e;

    try {
        if constexpr (is_cancellable_awaitable) {
            co_await f;
        } else if constexpr (std::is_same_v<std::remove_cvref_t<Future>, future<ReturnType>>) {
            if constexpr (std::is_void_v<ReturnType>) {
                co_await std::move(f).transport();
            } else {
                result_slot.put(co_await std::move(f).transport());
            }
        } else {
            if constexpr (std::is_void_v<ReturnType>) {
                co_await f;
            } else {
                result_slot.put(co_await f);
            }
        }
    } catch (...) { e = std::current_exception(); }

    if (base->barrier.try_acquire()) {
        l.won = true;
        struct lazy {
            const size_t index;
            std::shared_ptr<select_base> &base;
            ~lazy() {
                base->index = index;
                base->waiter.release();
            }
        } l{index, base};
        (void)l;

        co_await ctx->cancel();
        for (auto &n : base->notify_list) {
            if (n) {
                n->notify_all();
            }
        }

        if (e) {
            std::rethrow_exception(*e);
        } else {
            if constexpr (is_cancellable_awaitable || std::is_void_v<ReturnType>) {
                co_return;
            } else {
                co_return result_slot.move();
            }
        }
    } else {
        throw core::cancelled_exception{};
    }
}

template<typename... Results>
class select;

template<typename Result, typename... Results>
class select<Result, Results...> {
    friend class select<Results...>;

    constexpr static size_t branch_count = sizeof...(Results) + 1;

    using result_tuple_t = std::tuple<Result, Results...>;

    template<size_t I>
    using branch_value_t = monostate_if_void<std::tuple_element_t<(branch_count - 1) - I, result_tuple_t>>;

    template<size_t... Is>
    static auto variant_type_impl(std::index_sequence<Is...>)
        -> std::variant<branch<Is, branch_value_t<Is>>...>;

    using co_await_result_t = decltype(variant_type_impl(std::index_sequence_for<Result, Results...>{}));

public:
    template<typename... Args, cancellable_function<Args...> Fn>
    constexpr auto along_with(this select self, Fn &&fn, Args &&...args) {
        using result_type = cancellable_invoke_result_t<Fn, Args...>;
        return select<typename result_type::deliver_type, Result, Results...>{
            self.base, self.ctx,
            select_task(
                sizeof...(Results) + 1, self.base, self.ctx,
                co_invoke(std::forward<Fn>(fn), self.ctx, std::forward<Args>(args)...)),
            std::move(self.fs)};
    }

    constexpr auto along_with(this select self, cancellable_waitable auto &wait) {
        auto notify_ptr = &wait->get_notify();
        self.base->notify_list.push_back(notify_ptr);
        return select<void, Result, Results...>{
            self.base, self.ctx, select_task(sizeof...(Results) + 1, self.base, self.ctx, wait),
            std::move(self.fs)};
    }

    future<co_await_result_t> operator co_await() noexcept {
        co_await base->waiter.acquire();
        auto i = base->index;

        if (i == select_index_null || i >= branch_count) {
            panic::panic("[ASCO] select: invalid select result index");
        }

        using fn_t = future<co_await_result_t> (*)(select *) noexcept;
        constexpr auto table = []<size_t... Is>(std::index_sequence<Is...>) {
            return std::array<fn_t, branch_count>{&select::template await_branch<Is>...};
        }(std::make_index_sequence<branch_count>{});

        co_return co_await table[i](this);
    }

private:
    template<size_t I>
    static future<co_await_result_t> await_branch(select *self) noexcept {
        using branch_t = branch<I, branch_value_t<I>>;

        if constexpr (std::is_same_v<branch_value_t<I>, std::monostate>) {
            co_await self->result<I>();
            co_return co_await_result_t{std::in_place_type<branch_t>, std::monostate{}};
        } else {
            auto v = co_await self->result<I>();
            co_return co_await_result_t{std::in_place_type<branch_t>, std::move(v)};
        }
    }

    template<size_t I>
    auto result() {
        return std::move(std::get<sizeof...(Results) - I>(fs));
    }

    constexpr select(
        std::shared_ptr<select_base> base, std::shared_ptr<context> ctx, future_spawn<Result> f,
        std::tuple<future_spawn<Results>...> &&fs)
            : base{std::move(base)}
            , ctx{std::move(ctx)}
            , fs{std::tuple_cat(std::tuple(std::move(f)), std::move(fs))} {}

    std::shared_ptr<select_base> base;
    std::shared_ptr<context> ctx;
    std::tuple<future_spawn<Result>, future_spawn<Results>...> fs;
};

template<typename Result>
class select<Result> {
    friend class select<>;

public:
    template<typename... Args, cancellable_function<Args...> Fn>
    constexpr auto along_with(this select self, Fn &&fn, Args &&...args) {
        using result_type = cancellable_invoke_result_t<Fn, Args...>;
        return select<typename result_type::deliver_type, Result>{
            self.base, self.ctx,
            select_task(
                1, self.base, self.ctx,
                co_invoke(std::forward<Fn>(fn), self.ctx, std::forward<Args>(args)...)),
            std::tuple<future_spawn<Result>>{std::move(self.f)}};
    }

    constexpr auto along_with(this select self, cancellable_waitable auto &wait) {
        auto notify_ptr = &wait->get_notify();
        self.base->notify_list.push_back(notify_ptr);
        return select<void, Result>{
            self.base, self.ctx, select_task(1, self.base, self.ctx, wait),
            std::tuple<future_spawn<Result>>{std::move(self.f)}};
    }

    future<std::variant<branch<0, monostate_if_void<Result>>>> operator co_await() noexcept {
        co_await base->waiter.acquire();
        if constexpr (std::is_void_v<Result>) {
            co_await f;
            co_return {std::in_place_type<branch<0, std::monostate>>, std::monostate{}};
        } else {
            co_return {co_await f};
        }
    }

private:
    constexpr select(std::shared_ptr<select_base> base, std::shared_ptr<context> ctx, future_spawn<Result> f)
            : base{std::move(base)}
            , ctx{std::move(ctx)}
            , f{std::move(f)} {}

    std::shared_ptr<select_base> base;
    std::shared_ptr<context> ctx;
    future_spawn<Result> f;
};

template<>
class select<> {
public:
    select() {
        if (!core::worker::in_worker()) {
            panic::panic("[ASCO] select: select must be constructed inside a worker thread");
        }
    }

    select(const select &) = delete;
    select &operator=(const select &) = delete;

    ~select() noexcept = default;

    template<typename... Args, cancellable_function<Args...> Fn>
    constexpr auto along_with(this select, Fn &&fn, Args &&...args) {
        using result_type = cancellable_invoke_result_t<Fn, Args...>;
        auto base = std::make_shared<select_base>();
        auto ctx = context::with_cancel();
        return select<typename result_type::deliver_type>{
            base, ctx,
            select_task(0, base, ctx, co_invoke(std::forward<Fn>(fn), ctx, std::forward<Args>(args)...))};
    }

    constexpr auto along_with(this select, cancellable_waitable auto &wait) {
        auto base = std::make_shared<select_base>();
        auto ctx = context::with_cancel();
        auto notify_ptr = &wait->get_notify();
        base->notify_list.push_back(notify_ptr);
        return select<void>{base, ctx, select_task(0, base, ctx, wait)};
    }

    constexpr noop operator co_await() noexcept { return {}; }
};

};  // namespace asco::selects

namespace asco {

using selects::branch;
using selects::select;
using selects::select_index_null;

};  // namespace asco
