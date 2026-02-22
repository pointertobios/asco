// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <asco/core/cancellation.h>

namespace asco::this_task {

void close_cancellation() noexcept;

core::cancel_token &get_cancel_token() noexcept;

};  // namespace asco::this_task
