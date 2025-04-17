// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASCO_UTILS_TYPE_HASH_H
#define ASCO_UTILS_TYPE_HASH_H 1

#include <asco/utils/pubusing.h>

namespace asco {

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

};

#endif
