// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <random>

namespace asco::util {

inline auto &rng() {
    thread_local std::mt19937_64 gen([] {
        std::random_device rd;
        return rd();
    }());
    return gen;
}

};  // namespace asco::util
