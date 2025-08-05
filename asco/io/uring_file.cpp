// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/io/file.h>

#include <asco/core/linux/io_uring.h>
#include <asco/exception.h>

#include <chrono>
#include <coroutine>
#include <cstring>
#include <expected>
#include <optional>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

namespace asco::io {

using namespace core::_linux;

using clock = std::chrono::high_resolution_clock;

#define do_suspend_while(cond)                                       \
    for (auto start_time = clock::now(); cond; ({                    \
             if (clock::now() - start_time > file::max_waiting_time) \
                 co_await std::suspend_always{};                     \
         }))

future<std::expected<file, int>> open_file::open(
    std::string path, flags<file::options> opts, uint64_t perm, flags<file::resolve_mode> resolve) {
    flags<ioreq::open::open_mode> uring_openmode;
    {
        using open_mode = ioreq::open::open_mode;
        if (bool r = opts.has(file::options::read), w = opts.has(file::options::write); r && w)
            uring_openmode |= open_mode::read_write;
        else if (r)
            uring_openmode |= open_mode::readonly;
        else if (w)
            uring_openmode |= open_mode::writeonly;
        if (opts.has(file::options::append))
            uring_openmode |= open_mode::append;
        if (opts.has(file::options::create))
            uring_openmode |= open_mode::create;
        if (opts.has(file::options::exclusive))
            uring_openmode |= open_mode::exclusive;
        if (opts.has(file::options::truncate))
            uring_openmode |= open_mode::truncate;
        if (opts.has(file::options::nonblock))
            uring_openmode |= open_mode::nonblock;
        if (opts.has(file::options::nofollow))
            uring_openmode |= open_mode::nofollow;
        if (opts.has(file::options::tmpfile))
            uring_openmode |= open_mode::tmpfile;
        if (opts.has(file::options::path))
            uring_openmode |= open_mode::path;
        if (opts.has(file::options::noatime))
            uring_openmode |= open_mode::noatime;
        if (opts.has(file::options::sync))
            uring_openmode |= open_mode::sync;
        if (opts.has(file::options::dsync))
            uring_openmode |= open_mode::dsync;
        if (opts.has(file::options::rsync))
            uring_openmode |= open_mode::rsync;
    }

    flags<ioreq::open::resolve_mode> uring_resolvemode;
    {
        using resolve_mode = ioreq::open::resolve_mode;
        if (resolve.has(file::resolve_mode::no_cross_dev))
            uring_resolvemode |= resolve_mode::no_cross_dev;
        if (resolve.has(file::resolve_mode::no_magic_links))
            uring_resolvemode |= resolve_mode::no_magic_links;
        if (resolve.has(file::resolve_mode::no_symlinks))
            uring_resolvemode |= resolve_mode::no_symlinks;
        if (resolve.has(file::resolve_mode::beneath))
            uring_resolvemode |= resolve_mode::beneath;
        if (resolve.has(file::resolve_mode::in_root))
            uring_resolvemode |= resolve_mode::in_root;
        if (resolve.has(file::resolve_mode::cached))
            uring_resolvemode |= resolve_mode::cached;
    }

    auto &uring = RT::__worker::get_worker().get_uring();
    auto token = uring.submit(
        ioreq::open{
            path,
            uring_openmode,
            perm,
            uring_resolvemode,
        });

    do_suspend_while(true) {
        if (auto res = uring.peek(token.seq_num)) {
            if (*res < 0)
                co_return std::unexpected{*res};

            auto f = file{static_cast<int>(*res), std::move(path), opts, resolve};
            if (opts.has(file::options::append)) {
                f.seekg(0, seekpos::end);
                f.seekp(0, seekpos::end);
            }
            co_return std::move(f);
        }
    }
}

future_void file::close() {
    if (none)
        co_return {};

    none = true;
    fd = -1;

    auto &uring = RT::__worker::get_worker().get_uring();
    auto token = uring.submit(ioreq::close{fd});

    do_suspend_while(true) {
        if (uring.peek(token.seq_num))
            co_return {};
    }
}

future<std::optional<io::buffer<>>> file::write(buffer<> buf) {
    if (none)
        throw inner_exception("[ASCO] asco::io::file::write(): file not opened");

    if (!opts.has(options::write))
        throw inner_exception("[ASCO] asco::io::file::write(): file not opened with option::write");

    auto &uring = RT::__worker::get_worker().get_uring();
    auto token = uring.submit(ioreq::write{fd, std::move(buf), pwrite});

    do_suspend_while(true) {
        if (auto res = uring.peek(token.seq_num)) {
            if (*res < 0)
                throw inner_exception(
                    std::format(
                        "[ASCO] asco::io::file::write(): write failed because: {}", std::strerror(-*res)));

            pwrite += *res;
            co_return uring.peek_rest_write_buffer(token.seq_num, *res);
        }
    }
}

future<std::expected<buffer<>, file::read_result>> file::read(size_t nbytes) {
    if (none)
        throw inner_exception("[ASCO] asco::io::file::read(): file not opened");

    if (!opts.has(options::read))
        throw inner_exception("[ASCO] asco::io::file::read(): file not opened with option::read");

    struct re {
        file &self;
        int state{0};
        std::optional<core::_linux::uring::req_token> token{std::nullopt};
        int pread_inc{0};

        ~re() {
            if (!this_coro::aborted())
                return;

            this_coro::throw_coroutine_abort<future<io::buffer<>>>();

            switch (state) {
            case 1: {
                self.aborted_buffer = this_coro::move_back_return_value<future<io::buffer<>>>();
                self.pread -= pread_inc;
            } break;
            default:
                break;
            }
        }
    } restorer{*this};

    auto uring = &RT::__worker::get_worker().get_uring();
    core::_linux::uring::req_token token;

    if (aborted_token) {
        token = *aborted_token;
        aborted_token = std::nullopt;
        uring = &RT::get_runtime().get_worker_from_id(token.worker_id).get_uring();
        goto after_submit;
    }

    if (aborted_buffer) {
        restorer.state = 1;
        auto res = std::move(*aborted_buffer);
        aborted_buffer = std::nullopt;
        co_return std::move(res);
    }

    if (this_coro::aborted()) {
        throw base::coroutine_abort{};
    }

    token = uring->submit(ioreq::read{fd, nbytes, pread});

after_submit:

    do_suspend_while(true) {
        if (this_coro::aborted()) {
            aborted_token = token;
            throw base::coroutine_abort{};
        }

        uring = &RT::get_runtime().get_worker_from_id(token.worker_id).get_uring();

        if (auto res = uring->peek(token.seq_num)) {
            if (*res == 0) {
                co_return std::unexpected{file::read_result::eof};
            } else if (*res < 0) {
                switch (-*res) {
                case EINTR:
                    co_return std::unexpected{file::read_result::interrupted};
                case EAGAIN:
                    co_return std::unexpected{file::read_result::again};
                default:
                    throw inner_exception(
                        std::format(
                            "[ASCO] asco::io::file::read(): read failed because: {}", std::strerror(-*res)));
                }
            } else if (auto rbuf = uring->peek_read_buffer(token.seq_num, *res)) {
                restorer.state = 1;
                restorer.pread_inc = *res;
                pread += *res;
                co_return std::move(*rbuf);
            } else {
                throw inner_exception("[ASCO] asco::io::file::read(): cqe peeked but read buffer not found");
            }
        }
    }
}

};  // namespace asco::io
