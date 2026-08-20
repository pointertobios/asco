// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>

namespace asco::types {

template<typename T>
class raw_storage final {
public:
    raw_storage() = default;
    ~raw_storage() = default;

    raw_storage(const raw_storage &) = delete;
    raw_storage &operator=(const raw_storage &) = delete;

    raw_storage(raw_storage &&) = delete;
    raw_storage &operator=(raw_storage &&) = delete;

    template<typename... Args>
    T &emplace(Args &&...args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
        return *new (std::launder(storage)) T{std::forward<Args>(args)...};
    }

    void destroy() { std::launder(reinterpret_cast<T *>(storage))->~T(); }

    T *get() noexcept { return reinterpret_cast<T *>(storage); }
    const T *get() const noexcept { return reinterpret_cast<const T *>(storage); }

private:
    alignas(alignof(T)) std::byte storage[sizeof(T)];
};

template<>
class raw_storage<void> {};

};  // namespace asco::types
