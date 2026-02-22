// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <asco/util/types.h>

namespace asco {

template<util::types::move_secure T>
class future;

namespace core {

future<void> wait_for_valid(std::coroutine_handle<> handle);

};  // namespace core

};  // namespace asco
