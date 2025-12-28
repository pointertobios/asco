// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>

#include <asco/core/pool_allocator.h>
#include <asco/future.h>
#include <asco/invoke.h>
#include <asco/sync/notify.h>
#include <asco/sync/rwlock.h>
#include <asco/time/sleep.h>
#include <asco/utils/concepts.h>
#include <asco/utils/types.h>

namespace asco::contexts {

using namespace types;
using namespace concepts;

class context : std::enable_shared_from_this<context> {
public:
    using deliver_type = void;

    // !!! NEVER !!! construct from this function directly
    context() = default;

    static std::shared_ptr<context> with_cancel();

    static std::shared_ptr<context> with_timeout(const duration_type auto &dur) {
        auto res = std::allocate_shared<context>(core::mm::allocator<context>(context::_allocator));
        co_invoke([ctx = res, dur] -> future_spawn<void> {
            co_await sleep_for(dur);
            co_await ctx->cancel();
        }).ignore();
        return res;
    }

    context(const context &) = delete;
    context &operator=(const context &) = delete;

    ~context() noexcept = default;

    future<void> cancel();
    bool is_cancelled() const noexcept;

    yield<notify *> operator co_await() noexcept;

    // This callback must be reentrant
    future<void> set_cancel_callback(std::function<void()> &&callback);

    notify &get_notify() noexcept { return _notify; }

private:
    atomic_bool _cancelled{false};
    notify _notify;
    rwlock<std::function<void()>> _cancel_callback;

    static std::pmr::synchronized_pool_resource &_allocator;
};

inline std::pmr::synchronized_pool_resource &context::_allocator{core::mm::default_pool<context>()};

yield<notify *> operator co_await(const std::shared_ptr<context> &ctx) noexcept;

};  // namespace asco::contexts

namespace asco {

namespace concepts {

template<typename Fn, typename... Args>
concept cancellable_function = async_function<Fn, std::shared_ptr<contexts::context>, Args...>;

template<typename W>
concept cancellable_waitable = requires(W w) {
    { w->operator co_await() } -> std::same_as<yield<notify *>>;
    { w->get_notify() } -> std::same_as<notify &>;
    typename W::element_type::deliver_type;
    requires std::is_void_v<typename W::element_type::deliver_type>;
};

template<typename Fn, typename... Args>
    requires cancellable_function<Fn, Args...>
using cancellable_invoke_result_t = std::invoke_result_t<Fn, std::shared_ptr<contexts::context>, Args...>;

};  // namespace concepts

using contexts::context;

};  // namespace asco
