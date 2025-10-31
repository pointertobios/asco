// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/future.h>

asco::core::runtime &_ = asco::core::runtime::init();

#ifndef __ASCORT__

extern asco::future<int> async_main();

int main() { return async_main().spawn().await(); }

#endif
