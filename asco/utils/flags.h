// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_UTILS_FLAGS_H
#define ASCO_UTILS_FLAGS_H 1

#include <asco/utils/concepts.h>
#include <asco/utils/pubusing.h>

namespace asco::utils {

using namespace types;

// The enum members cannot have overlapped bit(s). Oneday C++26 come out, we would use reflection to assert
// this.
template<enum_type E>
class flags {
public:
    constexpr flags() = default;

    constexpr flags(E value)
            : value(static_cast<uint64_t>(value)) {}

    constexpr flags(uint64_t value)
            : value(value) {}

    constexpr flags(const flags &) = default;
    constexpr flags(flags &&) noexcept = default;
    constexpr flags &operator=(const flags &) = default;
    constexpr flags &operator=(flags &&) noexcept = default;

    constexpr friend flags operator|(E lhs, E rhs) {
        return flags(static_cast<uint64_t>(lhs) | static_cast<uint64_t>(rhs));
    }

    constexpr flags &operator|(E rhs) {
        value |= static_cast<uint64_t>(rhs);
        return *this;
    }

    constexpr flags &operator|=(E rhs) {
        value |= static_cast<uint64_t>(rhs);
        return *this;
    }

    constexpr flags &operator-=(E rhs) {
        value &= ~static_cast<uint64_t>(rhs);
        return *this;
    }

    bool has(E flag) const { return (value & static_cast<uint64_t>(flag)); }

    uint64_t raw() const { return value; }

    void clear() { value = 0; }

private:
    uint64_t value{0};
};

};  // namespace asco::utils

namespace asco {

template<enum_type E>
using flags = utils::flags<E>;

};

#endif
