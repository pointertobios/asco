// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <string_view>

namespace asco::util {

class type_id {
public:
    using type_hash = std::array<std::uint64_t, 2>;

    type_id()
            : m_hash{} {}

    type_id(const type_id &) = default;
    type_id &operator=(const type_id &) = delete;

    type_id(type_id &&) = default;
    type_id &operator=(type_id &&rhs) {
        if (this != &rhs) {
            this->~type_id();
            new (this) type_id(std::move(rhs));
        }
        return *this;
    }

    template<typename T>
    consteval static type_id of() noexcept {
        constexpr std::string_view funcname = __PRETTY_FUNCTION__;

        std::size_t start = funcname.find_first_of('=') + 2;
        std::size_t end = funcname.find_last_of(']');

        std::string_view name = funcname.substr(start, end - start);
        const auto hash = hash_str(name);
        return type_id{name, hash};
    }

    constexpr std::string_view name() const { return m_name; }

    constexpr const type_hash &hash() const { return m_hash; }

    constexpr bool operator==(const type_id &rhs) const noexcept {
        return
#ifndef NDEBUG
            this->m_name == rhs.m_name &&
#endif
            this->m_hash == rhs.m_hash;
    }

private:
    consteval type_id(std::string_view name, type_hash hash) noexcept
            : m_name{name}
            , m_hash{hash} {}

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

    static consteval type_hash hash_str(std::string_view str) noexcept {
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

    const std::string_view m_name;
    const type_hash m_hash;
};

};  // namespace asco::util
