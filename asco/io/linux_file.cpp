// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/io/file.h>

#include <asco/exception.h>
#include <asco/rterror.h>

namespace asco::io {

file::file_state file::state() const {
    struct stat st;
    if (fstat(fhandle, &st) == -1)
        throw inner_exception("[ASCO] asco::io::file::state(): fstat() failed.");

    return {st};
}

#ifndef ASCO_IO_URING
#endif

};  // namespace asco::io
