// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include "asco/core/runtime.h"
#include "asco/future.h"

#ifndef __ASCORT__

extern asco::future<int> async_main();

int main() {
    auto rt = asco::core::runtime_config{}.multi_threaded().build();
    return rt.block_on(async_main);
}

#endif
