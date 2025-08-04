// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_TIME_TIMEOUT_H
#define ASCO_TIME_TIMEOUT_H 1

#include <chrono>
#include <optional>

#include <asco/future.h>
#include <asco/select.h>
#include <asco/time/interval.h>
#include <asco/utils/concepts.h>

namespace asco::time {

using namespace concepts;

template<typename Ti, async_function F>
    requires std::is_same_v<Ti, std::chrono::duration<typename Ti::rep, typename Ti::period>>
future_inline<std::optional<typename std::invoke_result_t<F>::return_type>> timeout(Ti time, F f) {
    interval in{time};
    switch (co_await select<2>{}) {
    case 0: {
        co_return co_await f();
    }
    case 1: {
        co_await in.tick();
        co_return std::nullopt;
    }
    }
    std::unreachable();
}

};  // namespace asco::time

namespace asco {

using time::timeout;

};  // namespace asco

#endif
