// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_CORO_LOCAL_H
#define ASCO_CORO_LOCAL_H 1

#include <unordered_map>

namespace asco {

using size_t = std::size_t;

consteval size_t __consteval_str_hash(const char *name) {
    constexpr size_t offset_basis = 0xcbf29ce484222325ULL;
    constexpr size_t prime = 0x100000001b3ULL;
    size_t hash = offset_basis;
    for (const char* p = name; *p != '\0'; ++p) {
        hash ^= static_cast<size_t>(*p);
        hash *= prime;
    }
    return hash;
}

// The type hash only have to be different with different types,
// so just calculate the hash of function signature.
template<typename T>
consteval size_t type_hash() {
#if defined(__clang__) || defined(__GNUC__)
    constexpr auto name = __PRETTY_FUNCTION__;
#elif defined(_MSC_VER)
    constexpr auto name = __FUNCSIG__;
#endif
    constexpr auto p =  __consteval_str_hash(name);
    return p;
}

struct __coro_local_var {
    size_t type;
    void *p;
};

struct __coro_local_frame {
    __coro_local_frame *prev{nullptr};
    std::unordered_map<size_t, __coro_local_var> vars;

    __always_inline ~__coro_local_frame() {
        for (auto &[_, v] : vars) {
            try { delete v.p; } catch (...) {
                delete[] v.p;
            }
        }
    }

    template<typename T, size_t hash>
    T &get_var(const char *name) {
        if (auto it = vars.find(hash); it != vars.end()) {
            if (it->second.type != type_hash<T>())
                throw std::runtime_error("[ASCO] Coroutine local variable type mismatch");
            return *reinterpret_cast<T *>(it->second.p);
        } else if (prev) {
            return prev->get_var<T, hash>(name);
        } else {
            throw std::runtime_error(std::format("[ASCO] Coroutine local variable \'{}\' not found", name));
        }
    }

    template<typename T, size_t hash>
    T &decl_var(const char *name, T *pt) {
        if (auto it = vars.find(hash); it != vars.end()) {
            throw std::runtime_error(std::format("[ASCO] Coroutine local variable \'{}\' already declared", name));
        } else {
            vars[hash] = __coro_local_var{type_hash<T>(), pt};
            return *pt;
        }
    }

    template<typename T, size_t hash>
    T &decl_var(const char *name) {
        return decl_var<T, hash>(name, new T);
    }
};

};

#define coro_local(name)                                                                        \
    &name = RT::__worker::get_worker()->current_task().coro_local_frame                         \
        ->get_var<std::remove_reference_t<decltype(name)>, __consteval_str_hash(#name)>(#name);

#define decl_local_1arg(name)                                                                           \
    &name = RT::__worker::get_worker()->current_task().coro_local_frame                                 \
        ->decl_var<std::remove_reference_t<decltype(name)>, __consteval_str_hash(#name)>(#name)
#define decl_local_2arg(name, arg)                                                                      \
    &name = RT::__worker::get_worker()->current_task().coro_local_frame                                 \
        ->decl_var<std::remove_reference_t<decltype(name)>, __consteval_str_hash(#name)>(#name, arg)

#define dispatch_arg(_1, _2, NAME, ...) NAME
#define decl_local(...) \
    dispatch_arg(__VA_ARGS__, decl_local_2arg, decl_local_1arg)(__VA_ARGS__)

#endif
