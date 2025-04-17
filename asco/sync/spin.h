// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_SYNC_SPIN_H
#define ASCO_SYNC_SPIN_H 1

#include <asco/utils/pubusing.h>

namespace asco {

template<typename T>
class spin : private T {
public:
    using T::T;

    class guard {
        spin &s;
    public:
        guard(spin &s) : s{s} {
            for (bool b{false};
                !s.locked.compare_exchange_strong(b, true, morder::acquire, morder::relaxed););
        }

        ~guard() {
            s.locked.store(false, morder::release);
        }

        T &operator*() {
            return s;
        }

        const T &operator*() const {
            return s;
        }

        T *operator->() {
            return &s;
        }

        const T *operator->() const {
            return &s;
        }
    };

    guard lock() {
        return guard{*this};
    }

private:
    atomic_bool locked{false};
};

};

#endif
