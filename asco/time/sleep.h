// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_TIME_SLEEP_H
#define ASCO_TIME_SLEEP_H 1

#include <asco/time/interval.h>
#include <asco/utils/concepts.h>

namespace asco::this_coro {

future_inline<void> sleep_for(concepts::duration_type auto time) {
    interval in{time};
    co_await in.tick();
    co_return;
}

future_inline<void> sleep_until(concepts::time_point_type auto time) {
    auto now = decltype(time)::clock::now();

    if (now >= time)
        co_return;

    interval in{time - now};
    co_await in.tick();
    co_return;
}

};  // namespace asco::this_coro

#endif
