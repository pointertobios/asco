// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

namespace asco::panic {

inline const char *number_color{"\033[1;35m"};
inline const char *address_color{"\033[1;34m"};
inline const char *name_color{"\033[1;37m"};
inline const char *co_name_color{"\033[1;32m"};
inline const char *file_color{"\033[33m"};
inline const char *file_tail_color{"\033[1;33m"};
inline const char *lineno_color{"\033[1;34m"};
inline const char *reset_color{"\033[0m"};
inline const char *panic_color{"\033[1;31m"};

};  // namespace asco::panic
