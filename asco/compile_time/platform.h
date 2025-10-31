// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

namespace asco::compile_time::platform {

#ifdef linux
#    undef linux
#endif

namespace types {

enum class machine {
    x86_64,
    i386,
    aarch64,
    arm,
    ppc64,
    ppc,
    mips,
    loongarch64,
    loongarch,
};

enum class os {
    linux,
    windows,
    apple,
};

enum class compiler {
    gcc,
    clang,
    msvc,
    clang_cl,
};

};  // namespace types

struct platform {
    constexpr static types::machine _machine =
#if defined(__x86_64__) || defined(__x86_64) || defined(_M_X64)
        types::machine::x86_64
#elif defined(__i386__) || defined(__i386) || defined(_M_IX86)
        types::machine::i386
#elif defined(__aarch64__) || defined(_M_ARM64)
        types::machine::aarch64
#elif defined(__arm__) || defined(_M_ARM)
        types::machine::arm
#elif defined(__powerpc64__)
        types::machine::ppc64
#elif defined(__powerpc__)
        types::machine::ppc
#elif defined(__mips__)
        types::machine::mips
#elif defined(__loongarch__)
#    if _LOONGARCH_SZPTR == 64
        types::machine::loongarch64
#    elif _LOONGARCH_SZPTR == 32
        types::machine::loongarch
#    endif
#else
#    error [ASCO] Current machine type is not considered.
#endif
        ;
    consteval static types::machine machine() { return _machine; }
    consteval static bool machine_is(types::machine m) { return m == _machine; }

    constexpr static types::os _os =
#if defined(__linux__) || defined(__linux)
        types::os::linux
#elif defined(_WIN32) || defined(_WIN64)
        types::os::windows
#elif defined(__APPLE__)
        types::os::apple
#else
#    error [ASCO] Current OS is not considered.
#endif
        ;
    consteval static types::os os() { return _os; }
    consteval static bool os_is(types::os o) { return o == _os; }

    constexpr static types::compiler _compiler =
#if defined(__clang__)
        types::compiler::clang
#elif defined(_MSC_VER)
#    if defined(__clang__)
        types::compiler::clang_cl
#    else
        types::compiler::msvc
#        error [ASCO] Compile with clang-cl instead of MSVC
#    endif
#elif defined(__GNUC__)
        types::compiler::gcc
#else
#    error [ASCO] Current compiler is not considered.
#endif
        ;
    consteval static types::compiler compiler() { return _compiler; }

    consteval static int bit_width() { return sizeof(void *) * 8; }
};

using namespace types;

};  // namespace asco::compile_time::platform
