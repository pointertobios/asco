// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_LINUX_IO_URING_H
#define ASCO_LINUX_IO_URING_H 1

#include <asco/core/daemon.h>
#include <asco/core/sched.h>
#include <asco/core/slub.h>
#include <asco/io/buffer.h>
#include <asco/sync/spin.h>
#include <asco/utils/flags.h>
#include <asco/utils/pubusing.h>

#include <expected>
#include <flat_map>
#include <unordered_map>

#include <liburing.h>
#include <linux/openat2.h>
#include <poll.h>
#include <sys/eventfd.h>

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

    write(int fd, io::buffer<> &&buf, size_t offset)
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

class uring_poll_thread : public daemon {
public:
    uring_poll_thread(
        size_t worker_id, ::io_uring &ring, spin<std::unordered_map<size_t, ::io_uring_cqe>> &completed_cqes,
        atomic_int &event_fd)
            : daemon(std::format("asco::ringp{}", worker_id), SIGALRM)
            , ring(ring)
            , completed_cqes(completed_cqes)
            , event_fd(event_fd) {
        daemon::start();
    }

    bool attach(
        size_t seq_num, sched::task::task_id tid,
        spin<std::unordered_map<size_t, sched::task::task_id>>::guard guard);

    __asco_always_inline spin<std::unordered_map<size_t, sched::task::task_id>>::guard
    prelock_attaching_tasks() {
        return this->attaching_tasks.lock();
    }

    __asco_always_inline void decrease_unhandled_cqe_count() { unhandled_cqe_count.fetch_sub(1); }

private:
    bool initialize(atomic_bool &running) override;
    void run() override;

    ::io_uring &ring;
    spin<std::unordered_map<size_t, ::io_uring_cqe>> &completed_cqes;
    atomic_int &event_fd;
    ::pollfd pfds[1];
    atomic_ssize_t unhandled_cqe_count{0};

    spin<std::unordered_map<size_t, sched::task::task_id>> attaching_tasks;
};

class uring {
public:
    struct req_token {
        size_t worker_id;
        size_t seq_num;
    };

    explicit uring(size_t worker_id);
    ~uring();

    bool attach(
        size_t seq_num, sched::task::task_id tid,
        spin<std::unordered_map<size_t, sched::task::task_id>>::guard &&guard);
    std::optional<int> peek(size_t seq_num);
    std::expected<int, spin<std::unordered_map<size_t, sched::task::task_id>>::guard>
    peek_or_preattach(size_t seq_num);
    std::optional<io::buffer<>> peek_read_buffer(size_t seq_num, size_t read);
    std::optional<io::buffer<>> peek_rest_write_buffer(size_t seq_num, size_t written);

    req_token submit(ioreq::open);
    req_token submit(ioreq::close);
    req_token submit(ioreq::write);
    req_token submit(ioreq::read);

private:
    size_t worker_id;

    ::io_uring ring;
    atomic_int event_fd{-1};

    spin<std::unordered_map<size_t, ::io_uring_cqe>> completed_cqes;

    uring_poll_thread poll_thread;

    atomic_size_t seq_count{1};

    atomic_size_t iotask_count{0};

    // <req_token::seq_num, ioreq::open>
    spin<std::unordered_map<size_t, ioreq::open>> unpeeked_opens;

    // <req_token::seq_num, ioreq::write>
    spin<std::unordered_map<size_t, ioreq::write>> unpeeked_writes;

    struct read_buffer {
        constexpr static size_t unit_size = 1024;
        constexpr static size_t slub_max = 64;

        static slub::cache<read_buffer> slub_cache;

        void *operator new(size_t) noexcept { return slub_cache.allocate(); }
        void operator delete(void *ptr) noexcept { slub_cache.deallocate(static_cast<read_buffer *>(ptr)); }

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

    constexpr static size_t iovec_cache_unit_max = 64;
    // <length of iovec array, iovec array object>
    spin<std::flat_map<size_t, slub::object<::iovec> *>> iovec_cache;

    ::iovec *get_iovec(size_t len) noexcept;
    void drop_iovec(::iovec *ptr, size_t len) noexcept;

    constexpr static size_t io_uring_entries = 1024;
};

inline slub::cache<uring::read_buffer> uring::read_buffer::slub_cache{};

};  // namespace asco::core::_linux

#endif
