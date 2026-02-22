// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <new>

namespace asco::util {

template<typename T>
class raw_storage {
public:
    raw_storage() = default;
    ~raw_storage() = default;

    raw_storage(const raw_storage &) = delete;
    raw_storage &operator=(const raw_storage &) = delete;

    raw_storage(raw_storage &&) = delete;
    raw_storage &operator=(raw_storage &&) = delete;

    T *get() { return std::launder(reinterpret_cast<T *>(storage)); }
    const T *get() const { return std::launder(reinterpret_cast<const T *>(storage)); }

private:
    alignas(alignof(T)) unsigned char storage[sizeof(T)];
};

};  // namespace asco::util
