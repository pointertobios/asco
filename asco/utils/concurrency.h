// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_UTILS_CONCURRENCY_H
#define ASCO_UTILS_CONCURRENCY_H 1

#include <asco/utils/concepts.h>
#include <asco/utils/pubusing.h>

#if defined(_MSC_VER)
#    if defined(_M_IX86) || defined(_M_X64)
#        include <immintrin.h>  // _mm_pause
#    elif defined(_M_ARM) || defined(_M_ARM64)
#        include <intrin.h>  // __yield
#    endif
#endif

namespace asco::concurrency {

using namespace types;
using namespace concepts;

__asco_always_inline void cpu_relax() noexcept {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)
#    if defined(_MSC_VER)
    _mm_pause();
#    elif defined(__GNUC__) || defined(__clang__)
    __builtin_ia32_pause();
#    else
    asm volatile("pause");
#    endif
#elif defined(__aarch64__) || defined(_M_ARM64) || defined(__arm__) || defined(_M_ARM)
#    if defined(_MSC_VER)
    __yield();
#    elif defined(__GNUC__) || defined(__clang__)
    asm volatile("yield");
#    endif
#endif
}

template<size_t N>
__asco_always_inline void withdraw() noexcept {
    for (size_t i{0}; i < N; ++i) concurrency::cpu_relax();
}

template<typename T>
class atomic_ptr {
public:
    struct versioned_ptr {
        T *ptr{nullptr};
        size_t version{0};

        __asco_always_inline operator T *() noexcept { return ptr; }
        __asco_always_inline operator const T *() const noexcept { return ptr; }

        __asco_always_inline T *operator->() noexcept { return ptr; }
        __asco_always_inline const T *operator->() const noexcept { return ptr; }

        __asco_always_inline T &operator*() noexcept { return *ptr; }
        __asco_always_inline const T &operator*() const noexcept { return *ptr; }

        __asco_always_inline bool operator==(const versioned_ptr &rhs) const noexcept {
            return ptr == rhs.ptr && version == rhs.version;
        }

        __asco_always_inline bool operator==(T *rhs) const noexcept { return ptr == rhs; }

        __asco_always_inline friend bool operator==(T *lhs, const versioned_ptr &rhs) noexcept {
            return lhs == rhs.ptr;
        }
    };

    __asco_always_inline atomic_ptr() noexcept = default;
    __asco_always_inline ~atomic_ptr() noexcept = default;

    atomic_ptr(const atomic_ptr &) = delete;
    atomic_ptr &operator=(const atomic_ptr &) = delete;

    __asco_always_inline atomic_ptr(atomic_ptr &&other) noexcept {
        ptr = other.ptr;
        other.ptr = {nullptr};
    }

    __asco_always_inline atomic_ptr &operator=(atomic_ptr &&other) noexcept {
        if (this != &other) {
            ptr = other.ptr;
            other.ptr = nullptr;
        }
        return *this;
    }

    __asco_always_inline versioned_ptr load(morder mo) const noexcept { return ptr.load(mo); }

    __asco_always_inline void store(T *new_ptr, morder mo) noexcept {
        versioned_ptr curr_vp;
        do {
            curr_vp = ptr.load(mo);
        } while (!ptr.compare_exchange_weak(curr_vp, {new_ptr, curr_vp.version + 1}, mo, morder::relaxed));
    }

    __asco_always_inline bool
    compare_exchange_weak(versioned_ptr &expected, T *new_ptr, morder mo = morder::seq_cst) noexcept {
        return ptr.compare_exchange_weak(expected, {new_ptr, expected.version + 1}, mo);
    }

    __asco_always_inline bool
    compare_exchange_weak(versioned_ptr &expected, T *new_ptr, morder mo1, morder mo2) noexcept {
        return ptr.compare_exchange_weak(expected, {new_ptr, expected.version + 1}, mo1, mo2);
    }

    __asco_always_inline bool
    compare_exchange_strong(versioned_ptr &expected, T *new_ptr, morder mo = morder::seq_cst) noexcept {
        return ptr.compare_exchange_strong(expected, {new_ptr, expected.version + 1}, mo);
    }

    __asco_always_inline bool
    compare_exchange_strong(versioned_ptr &expected, T *new_ptr, morder mo1, morder mo2) noexcept {
        return ptr.compare_exchange_strong(expected, {new_ptr, expected.version + 1}, mo1, mo2);
    }

private:
    atomic<versioned_ptr> ptr{};
};

};  // namespace asco::concurrency

#endif
