// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/io/file.h>

#include <asco/exception.h>

namespace asco::io {

file::file(file &&rhs)
        : none{rhs.none}
        , fd{rhs.fd}
        , path{std::move(rhs.path)}
        , opts{rhs.opts}
        , resolve{rhs.resolve} {
    rhs.none = true;
    rhs.fd = -1;
    rhs.opts.clear();
    rhs.resolve.clear();
}

file::~file() {
    if (!none)
        close();
}

file::file(int fd, std::string path, flags<options> opts, flags<resolve_mode> resolve)
        : none{false}
        , fd{fd}
        , path{std::move(path)}
        , opts{opts}
        , resolve{resolve} {}

file &file::operator=(file &&rhs) {
    if (!none)
        close();

    none = rhs.none;
    fd = rhs.fd;
    path = std::move(rhs.path);
    opts = rhs.opts;
    resolve = rhs.resolve;

    rhs.none = true;
    rhs.fd = -1;
    rhs.opts.clear();
    rhs.resolve.clear();

    return *this;
}

future_void_inline
file::open(std::string path, flags<file::options> opts, uint64_t perm, flags<file::resolve_mode> resolve) {
    if (!none)
        throw inner_exception("[ASCP] asco::io::file::open(): called while a file already opened.");

    if (auto res = co_await open_file::open(std::move(path), opts, perm, resolve); res.has_value()) {
        *this = std::move(res.value());
    }

    co_return {};
}

future_void_inline file::reopen(opener &&o) {
    if (!none)
        co_await close();
    if (auto res = co_await std::move(o).open(); res.has_value()) {
        *this = std::move(res.value());
    }
    co_return {};
}

};  // namespace asco::io
