// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_UTILS_TYPE_HASH_H
#define ASCO_UTILS_TYPE_HASH_H 1

#include <asco/compile_time/string.h>
#include <asco/utils/pubusing.h>

namespace asco::inner {

// The type hash only have to be different with different types, so just calculate the hash of function
// signature.
template<typename T>
consteval size_t type_hash() {
#if defined(__clang__) || defined(__GNUC__)
    return compile_time::string{__PRETTY_FUNCTION__}.hash();
#elif defined(_MSC_VER)
    return compile_time::string{__FUNCSIG__}.hash();
#endif
}

};  // namespace asco::inner

#endif
