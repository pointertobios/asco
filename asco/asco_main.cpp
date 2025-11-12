// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/future.h>

inline asco::runtime_initializer_t runtime_initializer;

asco::core::runtime &_ = asco::core::runtime::init([] {
    if (runtime_initializer) {
        if (auto builder_func = *runtime_initializer; builder_func) {
            return builder_func();
        }
    }
    return asco::core::runtime_builder{};
}());

#ifndef __ASCORT__

extern asco::future<int> async_main();

int main() { return async_main().spawn().await(); }

#endif
