// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <new>

namespace asco::util {

// 无类型安全保证的类型擦除容器
class erased final {
public:
    erased() = default;

    template<typename T>
    erased(T &&value) noexcept
            : align{alignof(T)}
            , storage(::operator new(sizeof(T), std::align_val_t{alignof(T)}))
            , deleter(+[](void *ptr) { reinterpret_cast<T *>(ptr)->~T(); }) {
        new (storage) T(std::move(value));
    }

    erased(const erased &) = delete;
    erased &operator=(const erased &) = delete;

    erased(erased &&other) noexcept
            : align{other.align}
            , storage{other.storage}
            , deleter{other.deleter} {
        other.storage = nullptr;
        other.deleter = nullptr;
    }

    erased &operator=(erased &&other) noexcept {
        if (this != &other) {
            if (storage) {
                if (deleter) {
                    deleter(storage);
                }
                ::operator delete(storage, align);
            }

            align = other.align;
            storage = other.storage;
            deleter = other.deleter;

            other.storage = nullptr;
            other.deleter = nullptr;
        }
        return *this;
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
        if (storage) {
            if (deleter) {
                deleter(storage);
            }
            ::operator delete(storage, align);
        }
    }

private:
    std::align_val_t align;
    void *storage;
    void (*deleter)(void *);
};

};  // namespace asco::util
