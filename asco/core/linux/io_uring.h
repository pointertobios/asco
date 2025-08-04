// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_LINUX_IO_URING_H
#define ASCO_LINUX_IO_URING_H 1

#include <asco/core/slub.h>
#include <asco/io/buffer.h>
#include <asco/sync/spin.h>
#include <asco/utils/flags.h>
#include <asco/utils/pubusing.h>

#include <cstdint>
#include <deque>
#include <flat_map>
#include <unordered_map>

#include <liburing.h>
#include <linux/openat2.h>

namespace asco::core::_linux {

using namespace types;

namespace ioreq {

struct open {
    enum class open_mode {
        readonly = O_RDONLY,
        writeonly = O_WRONLY,
        read_write = O_RDWR,
        create = O_CREAT,
        exclusive = O_EXCL,
        truncate = O_TRUNC,
        append = O_APPEND,
        nonblock = O_NONBLOCK,
        dsync = O_DSYNC,
        sync = O_SYNC,
        rsync = O_RSYNC,
        directory = O_DIRECTORY,
        nofollow = O_NOFOLLOW,
        close_on_exec = O_CLOEXEC,
        tmpfile = O_TMPFILE,
        path = O_PATH,
        noatime = O_NOATIME,
        direct = O_DIRECT,
    };
    enum class resolve_mode {
        no_cross_dev = RESOLVE_NO_XDEV,
        no_magic_links = RESOLVE_NO_MAGICLINKS,
        no_symlinks = RESOLVE_NO_SYMLINKS,
        beneath = RESOLVE_BENEATH,
        in_root = RESOLVE_IN_ROOT,
        cached = RESOLVE_CACHED,
    };
    std::string path;
    flags<open_mode> mode;
    uint64_t perm;
    flags<resolve_mode> resolve;
};

struct close {
    int fd;
};

struct write {
    int fd;
    io::buffer<> buf;
    size_t offset;
    size_t nr_vecs{0};
    ::iovec *vec{nullptr};

    write(int fd, io::buffer<> buf, size_t offset)
            : fd(fd)
            , buf(std::move(buf))
            , offset(offset) {}

    write(write &&rhs) noexcept
            : fd(rhs.fd)
            , buf(std::move(rhs.buf))
            , nr_vecs(rhs.nr_vecs)
            , vec(rhs.vec) {
        rhs.fd = -1;
        rhs.nr_vecs = 0;
        rhs.vec = nullptr;
    }
};

struct read {
    int fd;
    size_t nbytes;
    size_t offset;
};

};  // namespace ioreq

class uring {
public:
    struct req_token {
        size_t worker_id;
        size_t seq_num;
    };

    explicit uring(size_t worker_id);
    ~uring();

    std::optional<int> peek(size_t seq_num);
    std::optional<io::buffer<>> peek_read_buffer(size_t seq_num, size_t read);
    std::optional<io::buffer<>> peek_rest_write_buffer(size_t seq_num, size_t written);

    req_token submit(ioreq::open);
    req_token submit(ioreq::close);
    req_token submit(ioreq::write);
    req_token submit(ioreq::read);

private:
    size_t worker_id;
    ::io_uring ring;

    atomic_size_t seq_count{0};

    atomic_size_t iotask_count{0};

    spin<std::deque<::io_uring_cqe *>> compeleted_queue;

    // <req_token::seq_num, ioreq::open>
    spin<std::unordered_map<size_t, ioreq::open>> unpeeked_opens;

    // <req_token::seq_num, ioreq::write>
    spin<std::unordered_map<size_t, ioreq::write>> unpeeked_writes;

    struct read_buffer {
        constexpr static size_t unit_size = 1024;
        constexpr static size_t slub_max = 64;

        static thread_local spin<slub::object<read_buffer> *> slub;

        void *operator new(size_t) noexcept;
        void operator delete(void *ptr_) noexcept;

        static void buffer_destroyer(char *ptr) noexcept;

        char buf[unit_size];
    };

    struct read_buffers_iovec {
        size_t nr_vecs;
        ::iovec *vec;

        io::buffer<> to_buffer(this read_buffers_iovec &&, size_t read);
    };

    // <req_token::seq_num, read_buffers_iovec>
    spin<std::unordered_map<size_t, read_buffers_iovec>> unpeeked_read_buffers;

    constexpr static size_t iovec_cache_unit_max = 8;
    // <length of iovec array, iovec array object>
    spin<std::flat_map<size_t, slub::object<::iovec> *>> iovec_cache;

    ::iovec *get_iovec(size_t len) noexcept;
    void drop_iovec(::iovec *ptr, size_t len) noexcept;

    constexpr static size_t io_uring_entries = 1024;
};

};  // namespace asco::core::_linux

#endif
