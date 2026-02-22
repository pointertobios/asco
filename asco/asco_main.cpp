// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/runtime.h>
#include <asco/future.h>

#ifndef __ASCORT__

extern asco::future<int> async_main();

int main() {
    asco::core::runtime rt;
    return rt.block_on(async_main);
}

#endif
