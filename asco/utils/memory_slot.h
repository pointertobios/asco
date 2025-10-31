// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <utility>

#include <asco/utils/concepts.h>
#include <asco/utils/types.h>

namespace asco::utils {

using namespace types;
using namespace concepts;

template<typename T>
struct memory_slot;

template<is_void T>
struct memory_slot<T> {
    char _[0];
};

template<non_void T>
struct memory_slot<T> {
    alignas(alignof(T)) unsigned char data[sizeof(T)];

    atomic_bool data_lives{false};

    void put(passing<T> value, bool neverdrop = false) noexcept {
        new (data) T(std::forward<T>(value));
        if (!neverdrop)
            data_lives.store(true, morder::release);
    }

    void emplace(bool neverdrop, auto &&...args) noexcept {
        new (data) T(std::forward<decltype(args)>(args)...);
        if (!neverdrop)
            data_lives.store(true, morder::release);
    }

    T &get() noexcept { return *reinterpret_cast<T *>(data); }

    const T &get() const noexcept { return *reinterpret_cast<const T *>(data); }

    T &&move() noexcept { return std::move(*reinterpret_cast<T *>(data)); }

    ~memory_slot() {
        if (data_lives.load(morder::acquire))
            get().~T();
    }
};

};  // namespace asco::utils

namespace asco {

using utils::memory_slot;

};
