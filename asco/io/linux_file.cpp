// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/io/file.h>

#include <asco/exception.h>
#include <asco/rterror.h>

namespace asco::io {

size_t file::_seek_fp(int fd, size_t &fp, ssize_t offset, seekpos whence) {
    switch (whence) {
    case seekpos::begin: {
        if (offset < 0)
            throw inner_exception(
                "[ASCO] asco::io::file::_seek_fp(): offset cannot be negative when seeking from begin.");

        fp = offset;
        return fp;
    }
    case seekpos::current: {
        struct stat st;
        if (fstat(fd, &st) == -1)
            throw inner_exception("[ASCO] asco::io::file::_seek_fp(): fstat() failed.");

        if (offset < 0 && static_cast<size_t>(-offset) > fp)
            throw inner_exception("[ASCO] asco::io::file::_seek_fp(): The file pointer is out of size.");
        else if (offset < 0 || fp + offset <= static_cast<size_t>(st.st_size))
            fp += offset;
        else
            throw inner_exception("[ASCO] asco::io::file::_seek_fp(): The file pointer is out of size.");

        return fp;
    }
    case seekpos::end: {
        struct stat st;
        if (fstat(fd, &st) == -1)
            throw inner_exception("[ASCO] asco::io::file::_seek_fp(): fstat() failed.");

        if (offset > 0)
            throw inner_exception(
                "[ASCO] asco::io::file::_seek_fp(): offset cannot be positive when seeking from end.");

        if (offset < 0 && static_cast<::__off_t>(-offset) > st.st_size)
            throw inner_exception("[ASCO] asco::io::file::_seek_fp(): The file pointer is out of size.");
        else
            fp = st.st_size + offset;

        return fp;
    }
    }
}

#ifndef ASCO_IO_URING
#endif

};  // namespace asco::io
