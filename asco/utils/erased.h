// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <new>

namespace asco::utils {

// No type security erased type wrapper
class erased {
public:
    template<typename T>
    erased(T &&value, void (*deleter)(void *)) noexcept
            : align{alignof(T)}
            , storage(::operator new(sizeof(T), std::align_val_t{alignof(T)}))
            , deleter(deleter) {
        new (storage) T(std::move(value));
    }

    template<typename T>
    T &get() noexcept {
        return *reinterpret_cast<T *>(storage);
    }

    template<typename T>
    const T &get() const noexcept {
        return *reinterpret_cast<const T *>(storage);
    }

    ~erased() {
        if (deleter) {
            deleter(storage);
        }
        ::operator delete(storage, align);
    }

private:
    std::align_val_t align;
    void *storage;
    void (*deleter)(void *);
};

};  // namespace asco::utils
