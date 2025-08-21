// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_IO_FILE_H
#define ASCO_IO_FILE_H 1

#include <asco/exception.h>
#include <asco/future.h>
#include <asco/io/buffer.h>
#include <asco/io/bufio.h>
#include <asco/utils/flags.h>

#include <cerrno>
#include <chrono>
#include <expected>
#include <optional>
#include <string>

namespace asco::io {

struct opener;
struct open_file;

class file {
    friend struct open_file;
    friend struct opener;

public:
#ifdef __linux__
    using file_raw_handle = int;
    constexpr static int raw_handle_invalid = -1;

    struct file_state {
        struct stat st;
        size_t size() const { return st.st_size; }
    };
#endif

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

    enum class open_result {
        not_found = ENOENT,
        not_directory = ENOTDIR,
        loop_symlink = ELOOP,
        name_too_long = ENAMETOOLONG,
        cannot_access = EACCES,
        permission_denied = EPERM,
        is_directory = EISDIR,
        read_only_fs = EROFS,
        txtbsy = ETXTBSY,
        max_file_process = EMFILE,
        max_file_system = ENFILE,
        cannot_specify_entry = ENOSPC,
        quota_exceeded = EDQUOT,
        no_memory = ENOMEM,
        invalid_arg = EINVAL,
        option_nosupport = EOPNOTSUPP,
        overflow = EOVERFLOW,
        nxio = ENXIO,  // fifo device unexist
        no_device = ENODEV,
        busy = EBUSY,
        again = EAGAIN,
        interrupted = EINTR,
    };

    file() = default;

    file(file &&);

    file(const file &) = delete;

    ~file();

    file &operator=(file &&);

    inline static constexpr opener at(std::string_view path) noexcept;
    inline static constexpr opener at() noexcept;  // Use for reopen file

    // Unabortable
    future_inline<void> open(std::string path, flags<file::options> opts, uint64_t perm = 0);

    // Platform related, unabortable
    future<void> close();

    // Unabortable
    future_inline<void> reopen(opener &&o);

    // Platform related, unabortable
    future<std::optional<buffer<>>> write(buffer<> buf);

    // Platform related
    future<std::expected<buffer<>, read_result>> read(size_t nbytes);

    __asco_always_inline size_t seekg(ssize_t offset, seekpos whence = seekpos::current) {
        return _seek_fp(pread, offset, whence, state().size());
    }

    __asco_always_inline size_t seekp(ssize_t offset, seekpos whence = seekpos::current) {
        return _seek_fp(pwrite, offset, whence, state().size());
    }

    __asco_always_inline size_t tellg() const noexcept { return pread; }
    __asco_always_inline size_t tellp() const noexcept { return pwrite; }

    __asco_always_inline file_raw_handle raw_handle() const noexcept { return fhandle; }

    size_t _seek_fp(size_t &fp, ssize_t offset, seekpos whence, size_t fsize);

    // Platform related
    file_state state() const;

private:
    file(int fhandle, std::string path, flags<file::options> opts);

    static constexpr std::chrono::microseconds max_waiting_time = std::chrono::microseconds(10);

    bool none{true};

    file_raw_handle fhandle{raw_handle_invalid};

    std::string path;
    flags<options> opts;

    size_t pread{0};
    size_t pwrite{0};

    bool is_destructor_close{false};
    // close() use this flag to inform the destructor that it can continue.
    atomic_flag destructor_can_exit;

#ifdef ASCO_IO_URING
    std::optional<core::_linux::uring::req_token> aborted_token{std::nullopt};
#endif
    std::optional<buffer<>> aborted_buffer{std::nullopt};
};

struct open_file {
    // Platform related, unabortable
    static future<std::expected<file, file::open_result>>
    open(std::string path, flags<file::options> opts, uint64_t perm = 0);
};

struct opener {
    std::string_view _path;
    flags<file::options> opts{file::options::read};
    uint64_t perm{};

    inline opener(std::string_view path)
            : _path(path) {}

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

    inline constexpr opener &&mode(this opener &&self, uint64_t perm) noexcept {
        self.perm = perm;
        return std::move(self);
    }

    inline future<std::expected<file, file::open_result>> open(this opener &&self) {
        if (!self.perm && self.opts.has(file::options::create))
            throw inner_exception("[ASCO] opener::open(): file access mode not set while creating file.");
        return open_file::open(std::string(self._path), self.opts, self.perm);
    }

    inline future_inline<void> reopen(this opener &&self, file &f) {
        self._path = f.path;
        return f.reopen(std::move(self));
    }
};

constexpr opener file::at(std::string_view path) noexcept { return {path}; }

constexpr opener file::at() noexcept { return {""}; }

#ifndef BUFIO_IMPL

extern template class block_read_writer<file>;
extern template class stream_reader<file>;
extern template class stream_writer<file>;

#endif

};  // namespace asco::io

namespace asco {

using io::file;

};  // namespace asco

#endif
