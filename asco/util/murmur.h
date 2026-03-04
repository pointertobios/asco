// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <bit>
#include <cstdint>

#include <asco/util/types.h>

namespace asco::util {

namespace detail {

using hash_val = std::array<std::uint64_t, 2>;

static consteval std::uint64_t fmix(std::uint64_t x) noexcept {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccd;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53;
    x ^= x >> 33;
    return x;
}

static consteval std::uint64_t concat64le(const char *source, std::size_t nbytes) noexcept {
    std::uint64_t result{0};
    for (std::size_t i = 0; i < nbytes; ++i) {
        result |= static_cast<std::uint64_t>(static_cast<unsigned char>(source[i])) << (i * 8);
    }
    return result;
}

static consteval hash_val hash_str(std::string_view str) noexcept {
    std::uint64_t len = str.size();

    constexpr std::uint64_t c1 = 0x87c37b91114253d5;
    constexpr std::uint64_t c2 = 0x4cf5ad432745937f;
    constexpr std::uint64_t k1ro = 31;
    constexpr std::uint64_t k2ro = 33;
    constexpr std::uint64_t h1ro = 27;
    constexpr std::uint64_t h2ro = 31;
    constexpr std::uint64_t n1 = 0x52dce729;
    constexpr std::uint64_t n2 = 0x38495ab5;

    std::uint64_t k1{0}, k2{0}, h1{0}, h2{0};

    for (std::size_t i = 0; i < len; i += 16) {
        k1 = concat64le(str.data() + i, std::min<std::size_t>(8, len - i));
        if (i + 8 < len) {
            k2 = concat64le(str.data() + i + 8, std::min<std::size_t>(8, len - i - 8));
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
    h1 = fmix(h1);
    h2 = fmix(h2);
    h1 += h2;
    h2 += h1;

    return {h1, h2};
}

};  // namespace detail

};  // namespace asco::util
