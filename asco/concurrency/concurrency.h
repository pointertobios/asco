// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <asco/utils/concepts.h>
#include <asco/utils/defines.h>
#include <asco/utils/types.h>

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

inline void cpu_relax() noexcept {
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
void withdraw() noexcept {
    for (size_t i{0}; i < N; ++i) concurrency::cpu_relax();
}

inline void exp_withdraw(size_t i) noexcept {
    switch (1 << i) {
    case 1:
        withdraw<1>();
        break;
    case 2:
        withdraw<2>();
        break;
    case 4:
        withdraw<4>();
        break;
    case 8:
        withdraw<8>();
        break;
    case 16:
        withdraw<16>();
        break;
    case 32:
        withdraw<32>();
        break;
    case 64:
        withdraw<64>();
        break;
    default:
        for (size_t j{0}; j < (1 << i) / 64; ++j) withdraw<64>();
        break;
    }
}

template<typename T>
class atomic_ptr {
public:
    struct versioned_ptr {
        T *ptr;
        size_t version{0};

        T *get_ptr() noexcept { return ptr; }

        operator T *() noexcept { return ptr; }
        operator const T *() const noexcept { return ptr; }

        T *operator->() noexcept { return ptr; }
        const T *operator->() const noexcept { return ptr; }

        T &operator*() noexcept { return *ptr; }
        const T &operator*() const noexcept { return *ptr; }

        bool operator==(const versioned_ptr &rhs) const noexcept {
            return ptr == rhs.ptr && version == rhs.version;
        }

        bool operator==(T *rhs) const noexcept { return ptr == rhs; }

        friend bool operator==(T *lhs, const versioned_ptr &rhs) noexcept { return lhs == rhs.ptr; }
    };

    atomic_ptr() noexcept = default;
    ~atomic_ptr() noexcept = default;

    atomic_ptr(T *ptr) noexcept
            : ptr{{ptr}} {}

    atomic_ptr(const atomic_ptr &) = delete;
    atomic_ptr &operator=(const atomic_ptr &) = delete;

    atomic_ptr(atomic_ptr &&other) noexcept {
        ptr.store(other.ptr, morder::release);
        other.ptr.store({nullptr}, morder::release);
    }

    atomic_ptr &operator=(atomic_ptr &&other) noexcept {
        if (this != &other) {
            ptr.store(other.ptr, morder::release);
            other.ptr.store({nullptr}, morder::release);
        }
        return *this;
    }

    versioned_ptr load(morder mo) const noexcept { return ptr.load(mo); }

    void store(T *new_ptr, morder mo) noexcept {
        versioned_ptr curr_vp;
        do {
            curr_vp = ptr.load(mo);
        } while (!ptr.compare_exchange_weak(curr_vp, {new_ptr, curr_vp.version + 1}, mo, morder::relaxed));
    }

    bool compare_exchange_weak(versioned_ptr &expected, T *new_ptr, morder mo = morder::seq_cst) noexcept {
        return ptr.compare_exchange_weak(expected, {new_ptr, expected.version + 1}, mo);
    }

    bool compare_exchange_weak(versioned_ptr &expected, T *new_ptr, morder mo1, morder mo2) noexcept {
        return ptr.compare_exchange_weak(expected, {new_ptr, expected.version + 1}, mo1, mo2);
    }

    bool compare_exchange_strong(versioned_ptr &expected, T *new_ptr, morder mo = morder::seq_cst) noexcept {
        return ptr.compare_exchange_strong(expected, {new_ptr, expected.version + 1}, mo);
    }

    bool compare_exchange_strong(versioned_ptr &expected, T *new_ptr, morder mo1, morder mo2) noexcept {
        return ptr.compare_exchange_strong(expected, {new_ptr, expected.version + 1}, mo1, mo2);
    }

private:
    atomic<versioned_ptr> ptr{{nullptr}};
};

template<>
class atomic_ptr<void> {
public:
    struct versioned_ptr {
        void *ptr;
        size_t version{0};

        void *get_ptr() noexcept { return ptr; }

        operator void *() noexcept { return ptr; }
        operator const void *() const noexcept { return ptr; }

        bool operator==(const versioned_ptr &rhs) const noexcept {
            return ptr == rhs.ptr && version == rhs.version;
        }

        bool operator==(void *rhs) const noexcept { return ptr == rhs; }

        friend bool operator==(void *lhs, const versioned_ptr &rhs) noexcept { return lhs == rhs.ptr; }
    };

    atomic_ptr() noexcept = default;
    ~atomic_ptr() noexcept = default;

    atomic_ptr(void *ptr) noexcept
            : ptr{{ptr}} {}

    atomic_ptr(const atomic_ptr &) = delete;
    atomic_ptr &operator=(const atomic_ptr &) = delete;

    atomic_ptr(atomic_ptr &&other) noexcept {
        ptr.store(other.ptr, morder::release);
        other.ptr.store({nullptr}, morder::release);
    }

    atomic_ptr &operator=(atomic_ptr &&other) noexcept {
        if (this != &other) {
            ptr.store(other.ptr, morder::release);
            other.ptr.store({nullptr}, morder::release);
        }
        return *this;
    }

    versioned_ptr load(morder mo) const noexcept { return ptr.load(mo); }

    void store(void *new_ptr, morder mo) noexcept {
        versioned_ptr curr_vp;
        do {
            curr_vp = ptr.load(mo);
        } while (!ptr.compare_exchange_weak(curr_vp, {new_ptr, curr_vp.version + 1}, mo, morder::relaxed));
    }

    bool compare_exchange_weak(versioned_ptr &expected, void *new_ptr, morder mo = morder::seq_cst) noexcept {
        return ptr.compare_exchange_weak(expected, {new_ptr, expected.version + 1}, mo);
    }

    bool compare_exchange_weak(versioned_ptr &expected, void *new_ptr, morder mo1, morder mo2) noexcept {
        return ptr.compare_exchange_weak(expected, {new_ptr, expected.version + 1}, mo1, mo2);
    }

    bool
    compare_exchange_strong(versioned_ptr &expected, void *new_ptr, morder mo = morder::seq_cst) noexcept {
        return ptr.compare_exchange_strong(expected, {new_ptr, expected.version + 1}, mo);
    }

    bool compare_exchange_strong(versioned_ptr &expected, void *new_ptr, morder mo1, morder mo2) noexcept {
        return ptr.compare_exchange_strong(expected, {new_ptr, expected.version + 1}, mo1, mo2);
    }

private:
    atomic<versioned_ptr> ptr{{nullptr}};
};

};  // namespace asco::concurrency
