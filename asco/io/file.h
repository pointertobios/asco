// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_IO_FILE_H
#define ASCO_IO_FILE_H 1

#include <asco/exception.h>
#include <asco/future.h>
#include <asco/io/buffer.h>
#include <asco/utils/flags.h>

#include <chrono>
#include <expected>
#include <optional>
#include <string>

namespace asco::io {

struct opener;
struct open_file;

class file {
    friend struct open_file;

public:
    enum class options {
        read = 1,
        write = 1 << 1,
        append = 1 << 2,
        create = 1 << 3,
        exclusive = 1 << 4,
        truncate = 1 << 5,
        nonblock = 1 << 6,
        nofollow = 1 << 7,
        tmpfile = 1 << 8,
        path = 1 << 9,
        noatime = 1 << 10,
        sync = 1 << 11,
        dsync = 1 << 12,
        rsync = 1 << 13,
    };

    enum class resolve_mode {
        no_cross_dev = 1,
        no_magic_links = 1 << 1,
        no_symlinks = 1 << 2,
        beneath = 1 << 3,
        in_root = 1 << 4,
        cached = 1 << 5,
    };

    file() = default;

    file(file &&);

    ~file();

    file &operator=(file &&);

    inline static constexpr opener at(std::string path) noexcept;

    // Unabortable
    future_void_inline open(
        std::string path, flags<file::options> opts, uint64_t perm = 0,
        flags<file::resolve_mode> resolve = flags<file::resolve_mode>{});

    // Platform related, unabortable
    future_void close();

    // Unabortable
    future_void_inline reopen(opener &&o);

    // Platform related, unabortable
    future<std::optional<io::buffer<>>> write(buffer<> buf);

    // Platform related
    future<buffer<>> read(size_t nbytes);

    enum class seekpos { begin, current, end };

    __asco_always_inline size_t seekg(ssize_t offset, seekpos whence = seekpos::current) {
        return _seek_fp(fd, pread, offset, whence);
    }

    __asco_always_inline size_t seekp(ssize_t offset, seekpos whence = seekpos::current) {
        return _seek_fp(fd, pwrite, offset, whence);
    }

    __asco_always_inline size_t tellg() const noexcept { return pread; }
    __asco_always_inline size_t tellp() const noexcept { return pwrite; }

private:
    file(int fd, std::string path, flags<file::options> opts, flags<file::resolve_mode> resolve);

    static size_t _seek_fp(int fd, size_t &fp, ssize_t offset, seekpos whence);

    static constexpr std::chrono::microseconds max_waiting_time = std::chrono::microseconds(10);

    bool none{true};

#ifdef __linux__
    int fd{-1};
#endif
    std::string path;
    flags<options> opts;
    flags<resolve_mode> resolve;

    size_t pread{0};
    size_t pwrite{0};

#ifdef ASCO_IO_URING
    std::optional<core::_linux::uring::req_token> aborted_token{std::nullopt};
#endif
    std::optional<buffer<>> aborted_buffer{std::nullopt};
};

struct open_file {
    // Platform related, unabortable
    static future<std::expected<file, int>> open(
        std::string path, flags<file::options> opts, uint64_t perm = 0,
        flags<file::resolve_mode> resolve = flags<file::resolve_mode>{});
};

struct opener {
    std::string _path;
    flags<file::options> opts{file::options::read};
    flags<file::resolve_mode> resolve{};
    uint64_t perm{};

    inline constexpr opener &&read(this opener &&self, bool b = true) noexcept {
        if (b)
            self.opts |= file::options::read;
        else
            self.opts -= file::options::read;
        return std::move(self);
    }

    inline constexpr opener &&write(this opener &&self, bool b = true) noexcept {
        if (b)
            self.opts |= file::options::write;
        else
            self.opts -= file::options::write;
        return std::move(self);
    }

    inline constexpr opener &&append(this opener &&self, bool b = true) noexcept {
        if (b)
            self.opts |= file::options::append;
        else
            self.opts -= file::options::append;
        return std::move(self);
    }

    inline constexpr opener &&create(this opener &&self, bool b = true) noexcept {
        if (b)
            self.opts |= file::options::create;
        else
            self.opts -= file::options::create;
        return std::move(self);
    }

    inline constexpr opener &&exclusive(this opener &&self, bool b = true) noexcept {
        if (b)
            self.opts |= file::options::exclusive;
        else
            self.opts -= file::options::exclusive;
        return std::move(self);
    }

    inline constexpr opener &&truncate(this opener &&self, bool b = true) noexcept {
        if (b)
            self.opts |= file::options::truncate;
        else
            self.opts -= file::options::truncate;
        return std::move(self);
    }

    inline constexpr opener &&nofollow(this opener &&self, bool b = true) noexcept {
        if (b)
            self.opts |= file::options::nofollow;
        else
            self.opts -= file::options::nofollow;
        return std::move(self);
    }

    inline constexpr opener &&tmpfile(this opener &&self, bool b = true) noexcept {
        if (b)
            self.opts |= file::options::tmpfile;
        else
            self.opts -= file::options::tmpfile;
        return std::move(self);
    }

    inline constexpr opener &&path(this opener &&self, bool b = true) noexcept {
        if (b)
            self.opts |= file::options::path;
        else
            self.opts -= file::options::path;
        return std::move(self);
    }

    inline constexpr opener &&noatime(this opener &&self, bool b = true) noexcept {
        if (b)
            self.opts |= file::options::noatime;
        else
            self.opts -= file::options::noatime;
        return std::move(self);
    }

    inline constexpr opener &&sync(this opener &&self, bool b = true) noexcept {
        if (b)
            self.opts |= file::options::sync;
        else
            self.opts -= file::options::sync;
        return std::move(self);
    }

    inline constexpr opener &&dsync(this opener &&self, bool b = true) noexcept {
        if (b)
            self.opts |= file::options::dsync;
        else
            self.opts -= file::options::dsync;
        return std::move(self);
    }

    inline constexpr opener &&rsync(this opener &&self, bool b = true) noexcept {
        if (b)
            self.opts |= file::options::rsync;
        else
            self.opts -= file::options::rsync;
        return std::move(self);
    }

    inline constexpr opener &&no_cross_dev(this opener &&self, bool b = true) noexcept {
        if (b)
            self.resolve |= file::resolve_mode::no_cross_dev;
        else
            self.resolve -= file::resolve_mode::no_cross_dev;
        return std::move(self);
    }

    inline constexpr opener &&no_magic_links(this opener &&self, bool b = true) noexcept {
        if (b)
            self.resolve |= file::resolve_mode::no_magic_links;
        else
            self.resolve -= file::resolve_mode::no_magic_links;
        return std::move(self);
    }

    inline constexpr opener &&no_symlinks(this opener &&self, bool b = true) noexcept {
        if (b)
            self.resolve |= file::resolve_mode::no_symlinks;
        else
            self.resolve -= file::resolve_mode::no_symlinks;
        return std::move(self);
    }

    inline constexpr opener &&beneath(this opener &&self, bool b = true) noexcept {
        if (b)
            self.resolve |= file::resolve_mode::beneath;
        else
            self.resolve -= file::resolve_mode::beneath;
        return std::move(self);
    }

    inline constexpr opener &&in_root(this opener &&self, bool b = true) noexcept {
        if (b)
            self.resolve |= file::resolve_mode::in_root;
        else
            self.resolve -= file::resolve_mode::in_root;
        return std::move(self);
    }

    inline constexpr opener &&cached(this opener &&self, bool b = true) noexcept {
        if (b)
            self.resolve |= file::resolve_mode::cached;
        else
            self.resolve -= file::resolve_mode::cached;
        return std::move(self);
    }

    inline constexpr opener &&mode(this opener &&self, uint64_t perm) noexcept {
        self.perm = perm;
        return std::move(self);
    }

    inline future<std::expected<file, int>> open(this opener &&self) {
        if (!self.perm && self.opts.has(file::options::create))
            throw inner_exception("[ASCO] opener::open(): file access mode not set while creating file.");
        return open_file::open(std::move(self._path), self.opts, self.perm, self.resolve);
    }

    inline future_void_inline reopen(this opener &&self, file &f) { return f.reopen(std::move(self)); }
};

constexpr opener file::at(std::string path) noexcept { return {std::move(path)}; }

};  // namespace asco::io

namespace asco {

using file = io::file;

};

#endif
