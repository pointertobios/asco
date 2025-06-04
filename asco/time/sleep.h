// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_TIME_SLEEP_H
#define ASCO_TIME_SLEEP_H 1

#include <asco/time/interval.h>

namespace asco::this_coro {

template<typename Ti>
    requires std::is_same_v<Ti, std::chrono::duration<typename Ti::rep, typename Ti::period>>
future_void_inline sleep_for(Ti time) {
    interval in{time};
    co_await in.tick();
    co_return {};
}

template<typename Tc>
    requires std::is_same_v<Tc, std::chrono::time_point<typename Tc::clock, typename Tc::duration>>
future_void_inline sleep_until(Tc time) {
    auto now = Tc::clock::now();

    if (now >= time)
        co_return {};

    interval in{time - now};
    co_await in.tick();
    co_return {};
}

};  // namespace asco::this_coro

#endif
