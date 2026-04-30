// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <exception>
#include <expected>
#include <type_traits>
#include <utility>

#include <asco/util/types.h>

namespace asco::task {

template<util::types::move_secure T>
T fetch_result(std::expected<T, std::exception_ptr> &&e) {
    if (e) {
        if constexpr (std::is_void_v<T>) {
            return;
        } else {
            return std::move(*e);
        }
    } else {
        std::rethrow_exception(e.error());
    }
}

};  // namespace asco::task
