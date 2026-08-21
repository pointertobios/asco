// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

namespace asco {

#ifdef _MSC_VER
#    define ASCO_NO_UNIQUE_ADDRESS msvc::no_unique_address
#else
#    define ASCO_NO_UNIQUE_ADDRESS no_unique_address
#endif

};  // namespace asco
