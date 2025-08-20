// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <functional>
#include <iostream>
#include <print>
#include <typeinfo>

#include <asco/exception.h>
#include <asco/future.h>
#include <asco/lazy_delete.h>
#include <asco/utils/pubusing.h>

using asco::future, asco::lazy_delete;
using asco::core::runtime;

extern future<int> async_main();

inline asco::runtime_initializer_t runtime_initializer;

namespace asco_main {

lazy_delete<runtime> __rt([] {
    runtime *rt;
    if (runtime_initializer) {
        rt = (*runtime_initializer)();
    } else {
        rt = new runtime();
    }
    return rt;
});

};  // namespace asco_main

#ifndef __ASCORT__

int main(int argc, const char **argv, const char **env) {
    runtime::sys::set_args(argc, argv);
    runtime::sys::set_env(env);
    try {
        return async_main().await();
    } catch (std::exception &e) {
        std::println(
            std::cerr, "\033[1;38;2;128;128;128m[asco-main]\033[0m \033[1;31m{}\033[0m: {}",
            asco::inner::demangle(typeid(e).name()), e.what());
        return -1;
    } catch (...) {
        std::println(
            std::cerr, "\033[1;38;2;128;128;128m[asco-main]\033[0m \033[1;31mUnknown exception\033[0m");
        return -2;
    }
}

#endif
