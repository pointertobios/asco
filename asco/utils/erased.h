// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <vector>

namespace asco::utils {

// No type security erased type wrapper
class erased {
public:
    template<typename T>
    erased(T &&value, void (*deleter)(void *)) noexcept
            : storage(sizeof(T))
            , deleter(deleter) {
        new (storage.data()) T(std::move(value));
    }

    template<typename T>
    T &get() noexcept {
        return *reinterpret_cast<T *>(storage.data());
    }

    template<typename T>
    const T &get() const noexcept {
        return *reinterpret_cast<const T *>(storage.data());
    }

    ~erased() {
        if (deleter) {
            deleter(storage.data());
        }
    }

private:
    std::vector<std::byte> storage;
    void (*deleter)(void *);
};

};  // namespace asco::utils
