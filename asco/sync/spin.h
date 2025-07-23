// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_SYNC_SPIN_H
#define ASCO_SYNC_SPIN_H 1

#include <asco/utils/pubusing.h>

namespace asco::sync {

using namespace asco::types;

template<typename T>
class spin {
public:
    spin()
            : value{} {}

    spin(const spin &) = delete;
    spin(spin &&) = delete;

    explicit spin(const T &val)
            : value{val} {}

    explicit spin(T &&val)
            : value{std::move(val)} {}

    template<typename... Args>
    explicit spin(Args &&...args)
            : value(std::forward<Args>(args)...) {}

    class guard {
        spin &s;

    public:
        guard(spin &s)
                : s{s} {
            for (bool b{false}; !s.locked.compare_exchange_weak(b, true, morder::acquire, morder::relaxed);
                 b = false);
        }

        ~guard() { s.locked.store(false, morder::release); }

        T &operator*() noexcept { return s.value; }

        const T &operator*() const noexcept { return s.value; }

        T *operator->() noexcept { return &s.value; }

        const T *operator->() const noexcept { return &s.value; }
    };

    guard lock() noexcept { return guard{*this}; }

    T &&get() noexcept { return std::move(value); }

private:
    T value;
    atomic_bool locked{false};
};

};  // namespace asco::sync

namespace asco {

using sync::spin;

}

#endif
