// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <bit>
#include <cmath>

#include "asco/types/int.h"

namespace asco::util {

using hash_val = std::array<u64, 2>;

inline constexpr u64 mix64(u64 x) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccd;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53;
    x ^= x >> 33;
    return x;
}

inline constexpr u64 concat64le(const char *source, usize nbytes) {
    u64 result{0};
    for (usize i = 0; i < nbytes; ++i) {
        result |= static_cast<u64>(static_cast<unsigned char>(source[i])) << (i * 8);
    }
    return result;
}

inline constexpr hash_val hash_str(std::string_view str) {
    u64 len = str.size();

    constexpr u64 c1 = 0x87c37b91114253d5;
    constexpr u64 c2 = 0x4cf5ad432745937f;
    constexpr u64 k1ro = 31;
    constexpr u64 k2ro = 33;
    constexpr u64 h1ro = 27;
    constexpr u64 h2ro = 31;
    constexpr u64 n1 = 0x52dce729;
    constexpr u64 n2 = 0x38495ab5;

    u64 k1{0}, k2{0}, h1{0}, h2{0};

    for (usize i = 0; i < len; i += 16) {
        k1 = concat64le(str.data() + i, std::min<usize>(8, len - i));
        if (i + 8 < len) {
            k2 = concat64le(str.data() + i + 8, std::min<usize>(8, len - i - 8));
        } else {
            k2 = 0;
        }

        k1 *= c1;
        k2 *= c2;
        k1 = std::rotl(k1, k1ro);
        k2 = std::rotl(k2, k2ro);
        k1 *= c2;
        k2 *= c1;
        h1 ^= k1;
        h2 ^= k2;
        h1 = std::rotl(h1, h1ro);
        h2 = std::rotl(h2, h2ro);
        h1 += h2;
        h2 += h1;
        h1 = h1 * 5 + n1;
        h2 = h2 * 5 + n2;
    }
    h1 ^= len;
    h2 ^= len;
    h1 += h2;
    h2 += h1;
    h1 = mix64(h1);
    h2 = mix64(h2);
    h1 += h2;
    h2 += h1;

    return {h1, h2};
}

};  // namespace asco::util
