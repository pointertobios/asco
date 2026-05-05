// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <iterator>
#include <type_traits>
#include <utility>

namespace asco::util {

template<typename E>
struct flag_enum_traits;

namespace detail {

template<typename E>
consteval bool fits_flag_enum() {
    static_assert(std::is_enum_v<E>, "E must be an enum type");
    static_assert(
        std::forward_iterator<decltype(flag_enum_traits<E>::values)>
            || std::is_array_v<decltype(flag_enum_traits<E>::values)>,
        "flag_enum_traits<E>::values must be a forward iterable container");
    for (auto v : flag_enum_traits<E>::values) {
        if (static_cast<std::size_t>(std::to_underlying(v)) >= sizeof(std::size_t) * 8) {
            return false;
        }
    }
    return true;
}

};  // namespace detail

template<typename E>
concept flags_enum = std::is_enum_v<E> && detail::fits_flag_enum<E>();

template<flags_enum E>
class flags {
    static std::size_t bit_of(E e) { return 1 << std::to_underlying(e); }

public:
    flags() = default;

    flags(E e)
            : m_bits{bit_of(e)} {}

    flags operator|(E e) const { return {m_bits | bit_of(e)}; }

    flags operator|(flags other) const { return {m_bits | other.m_bits}; }

    flags &operator|=(E e) {
        m_bits |= bit_of(e);
        return *this;
    }

    flags &operator|=(flags other) {
        m_bits |= other.m_bits;
        return *this;
    }

    flags operator-(E e) const { return {m_bits & ~bit_of(e)}; }

    flags &operator-=(E e) {
        m_bits &= ~bit_of(e);
        return *this;
    }

    bool contains(E e) const { return (m_bits & bit_of(e)) == bit_of(e); }

    bool contains(flags other) const { return (m_bits & other.m_bits) == other.m_bits; }

    std::size_t underlying() const { return m_bits; }

private:
    flags(std::size_t bits)
            : m_bits{bits} {}

    std::size_t m_bits{0};
};

#define _ASCO_UTIL_FLAGS_ENABLE_BITWISE_OPS(Enum)                                                 \
    inline asco::util::flags<Enum> operator|(Enum a, Enum b) { return asco::util::flags{a} | b; } \
    inline asco::util::flags<Enum> operator|(Enum a, asco::util::flags<Enum> b) {                 \
        return asco::util::flags{a} | b;                                                          \
    }

};  // namespace asco::util
