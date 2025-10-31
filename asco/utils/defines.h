// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#ifdef __linux__
#    define asco_always_inline __inline __attribute__((__always_inline__))
#elif defined(_WIN32)
#    define asco_always_inline __forceinline
#endif
