// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/io/file.h>

#include <asco/exception.h>

namespace asco::io {

file::file(file &&rhs)
        : none{rhs.none}
#ifdef __linux__
        , fhandle{rhs.fhandle}
#endif
        , path{std::move(rhs.path)}
        , opts{rhs.opts}
        , pread{rhs.pread}
        , pwrite{rhs.pwrite}
#ifdef ASCO_IO_URING
        , aborted_token{std::move(rhs.aborted_token)}
#endif
        , aborted_buffer{std::move(rhs.aborted_buffer)} {
    rhs.none = true;
    rhs.fhandle = raw_handle_invalid;
    rhs.opts.clear();
    rhs.pread = 0;
    rhs.pwrite = 0;
}

file::~file() {
    if (none)
        return;

    is_destructor_close = true;
    close();
    while (!destructor_can_exit.test());
}

file::file(int fd, std::string path, flags<options> opts)
        : none{false}
#ifdef __linux__
        , fhandle{fd}
#endif
        , path{std::move(path)}
        , opts{opts} {
}

file &file::operator=(file &&rhs) {
    if (!none)
        close();

    none = rhs.none;
#ifdef __linux__
    fhandle = rhs.fhandle;
#endif
    path = std::move(rhs.path);
    opts = rhs.opts;
    pread = rhs.pread;
    pwrite = rhs.pwrite;
#ifdef ASCO_IO_URING
    aborted_token = std::move(rhs.aborted_token);
#endif
    aborted_buffer = std::move(rhs.aborted_buffer);

    rhs.none = true;
    rhs.fhandle = raw_handle_invalid;
    rhs.opts.clear();
    rhs.pread = 0;
    rhs.pwrite = 0;

    return *this;
}

future_inline<void> file::open(std::string path, flags<file::options> opts, uint64_t perm) {
    if (!none)
        throw inner_exception("[ASCP] asco::io::file::open(): called while a file already opened.");

    if (auto res = co_await open_file::open(std::move(path), opts, perm); res.has_value()) {
        *this = std::move(res.value());
    }

    co_return;
}

future_inline<void> file::reopen(opener &&o) {
    if (!none)
        co_await close();
    if (auto res = co_await std::move(o).open(); res.has_value()) {
        *this = std::move(res.value());
    }
    co_return;
}

size_t file::_seek_fp(size_t &fp, ssize_t offset, seekpos whence, size_t fsize) {
    switch (whence) {
    case seekpos::begin: {
        if (offset < 0)
            throw inner_exception(
                "[ASCO] asco::io::file::_seek_fp(): offset cannot be negative when seeking from begin.");

        fp = offset;
        return fp;
    }
    case seekpos::current: {
        if (offset < 0 && static_cast<size_t>(-offset) > fp)
            throw inner_exception("[ASCO] asco::io::file::_seek_fp(): The file pointer is out of size.");
        else if (offset < 0 || fp + offset <= static_cast<size_t>(fsize))
            fp += offset;
        else
            throw inner_exception("[ASCO] asco::io::file::_seek_fp(): The file pointer is out of size.");

        return fp;
    }
    case seekpos::end: {
        if (offset > 0)
            throw inner_exception(
                "[ASCO] asco::io::file::_seek_fp(): offset cannot be positive when seeking from end.");

        if (offset < 0 && static_cast<size_t>(-offset) > fsize)
            throw inner_exception("[ASCO] asco::io::file::_seek_fp(): The file pointer is out of size.");
        else
            fp = fsize + offset;

        return fp;
    }
    }
}

};  // namespace asco::io
