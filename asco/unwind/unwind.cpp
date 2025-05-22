// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <asco/unwind/unwind.h>

#include <execinfo.h>

namespace asco::unwind {

stacktrace function_unwinder::unwind() {
    function_unwinder unwinder;
    void *stackrets[128];
    int len = ::backtrace(stackrets, 128);
    // The last 2 stack frames in non-main threads can lead to a segmentation fault crash with clang
    // compiling. At the same time, the main thread has 3 uneeded frames. So just ignore the last 2 frames.
    for (int i{0}; i < len - 2; i++)
        unwinder.stacks.push_back(unwinder.resolve_address((size_t)stackrets[i]));
    return {std::move(unwinder.stacks)};
}

void *unwind_index(size_t index) {
    void **stackrets = (void **)new size_t[index + 1];
    ::backtrace(stackrets, index + 1);
    return stackrets[index];
}

};  // namespace asco::unwind
