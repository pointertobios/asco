// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_LINUX_IO_URING_HELPER_H
#define ASCO_LINUX_IO_URING_HELPER_H 1

#include <asco/core/linux/io_uring.h>
#include <asco/future.h>

namespace asco::core::_linux {

// Abortable, but does not throw asco::coroutine_abort
future_inline<int> peek_uring(uring &ring, size_t seq_num);

};  // namespace asco::core::_linux

#endif
