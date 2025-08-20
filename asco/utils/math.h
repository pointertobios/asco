// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_UTILS_MATH_H
#define ASCO_UTILS_MATH_H 1

#include <asco/utils/pubusing.h>

#include <bit>
#include <cassert>

namespace asco::math {

using namespace types;

constexpr size_t min_exp2_from(size_t n) {
    assert(n > 0);
    if (n == 1)
        return 1;
    return static_cast<size_t>(1) << std::bit_width(n - 1);
}

};  // namespace asco::math

__asco_always_inline consteval size_t pow2(size_t base, size_t exp) noexcept { return base << exp; }

namespace asco {

using math::min_exp2_from;

};

#endif
