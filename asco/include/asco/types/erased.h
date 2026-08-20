// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <new>
#include <type_traits>
#include <utility>

namespace asco::types {

class erased final {
    template<typename T>
    struct ref {
        T &v;
    };

public:
    erased() noexcept = default;

    template<typename T, typename... Args>
    static erased create(Args &&...args) noexcept(std::is_nothrow_constructible_v<T, Args...>) {
        auto obj = ::operator new(sizeof(T), std::align_val_t{alignof(T)});
        new (obj) T(std::forward<Args>(args)...);
        return {alignof(T), obj, default_deleter<T>};
    }

    static erased refer(auto &value) noexcept { return {ref{value}}; }

    erased(const erased &) = delete;
    erased &operator=(const erased &) = delete;

    erased(erased &&rhs) noexcept
            : m_align{rhs.m_align}
            , m_storage{rhs.m_storage}
            , m_deleter{rhs.m_deleter} {
        rhs.m_storage = nullptr;
        rhs.m_deleter = nullptr;
    }

    erased &operator=(erased &&rhs) noexcept {
        if (this != &rhs) {
            this->~erased();
            new (this) erased{std::move(rhs)};
        }
        return *this;
    }

    operator bool() const noexcept { return m_storage; }

    template<typename T>
    T &get() noexcept {
        return *reinterpret_cast<T *>(m_storage);
    }

    template<typename T>
    const T &get() const noexcept {
        return *reinterpret_cast<const T *>(m_storage);
    }

    ~erased() {
        if (m_storage) {
            if (m_deleter) {
                m_deleter(m_storage);
                ::operator delete(m_storage, m_align);
            }
        }
    }

private:
    erased(std::align_val_t align, void *storage, void (*deleter)(void *)) noexcept
            : m_align{align}
            , m_storage{storage}
            , m_deleter{deleter} {}

    template<typename T>
    erased(ref<T> &&value) noexcept
            : m_align{alignof(T)}
            , m_storage{&value.v}
            , m_deleter{nullptr} {}

    template<typename T>
    static void default_deleter(void *ptr) {
        reinterpret_cast<T *>(ptr)->~T();
    }

    std::align_val_t m_align;
    void *m_storage{nullptr};
    void (*m_deleter)(void *){nullptr};
};

};  // namespace asco::types
