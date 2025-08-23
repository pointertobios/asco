// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_TIME_TIMEOUT_H
#define ASCO_TIME_TIMEOUT_H 1

#include <optional>

#include <asco/future.h>
#include <asco/select.h>
#include <asco/time/interval.h>
#include <asco/utils/concepts.h>

namespace asco::time {

using namespace concepts;

auto timeout(duration_type auto time, async_function auto f) -> future_inline<
    std::optional<monostate_if_void<typename std::invoke_result_t<decltype(f)>::return_type>>> {
    interval in{time};
    switch (co_await select<2>{}) {
    case 0: {
        if constexpr (std::is_void_v<typename std::invoke_result_t<decltype(f)>::return_type>) {
            co_await f();
            co_return std::monostate{};
        } else {
            co_return co_await f();
        }
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
