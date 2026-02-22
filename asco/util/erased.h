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
            : m_align{alignof(T)}
            , m_storage(::operator new(sizeof(T), std::align_val_t{alignof(T)}))
            , m_deleter(default_deleter<T>) {
        new (m_storage) T(std::move(value));
    }

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
            if (m_storage) {
                if (m_deleter) {
                    m_deleter(m_storage);
                }
                ::operator delete(m_storage, m_align);
            }

            m_align = rhs.m_align;
            m_storage = rhs.m_storage;
            m_deleter = rhs.m_deleter;

            rhs.m_storage = nullptr;
            rhs.m_deleter = nullptr;
        }
        return *this;
    }

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
            }
            ::operator delete(m_storage, m_align);
        }
    }

private:
    template<typename T>
    static void default_deleter(void *ptr) {
        reinterpret_cast<T *>(ptr)->~T();
    }

    std::align_val_t m_align;
    void *m_storage{nullptr};
    void (*m_deleter)(void *){nullptr};
};

};  // namespace asco::util
