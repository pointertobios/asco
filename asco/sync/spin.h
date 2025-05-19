// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_SYNC_SPIN_H
#define ASCO_SYNC_SPIN_H 1

#include <asco/utils/pubusing.h>

namespace asco::sync {

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

        T &operator*() { return s.value; }

        const T &operator*() const { return s.value; }

        T *operator->() { return &s.value; }

        const T *operator->() const { return &s.value; }
    };

    guard lock() { return guard{*this}; }

    T &&get() { return std::move(value); }

private:
    T value;
    atomic_bool locked{false};
};

};  // namespace asco::sync

namespace asco {

using sync::spin;

}

#endif
