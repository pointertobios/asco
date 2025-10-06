// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/io/file.h>

#include <asco/core/linux/io_uring.h>
#include <asco/core/linux/io_uring_helper.h>
#include <asco/exception.h>

#include <cerrno>
#include <cstring>
#include <expected>
#include <optional>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

namespace asco::io {

using namespace core::_linux;

future<std::expected<file, file::open_result>>
open_file::open(std::string path, flags<file::options> opts, uint64_t perm) {
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

    auto &uring = RT::__worker::get_worker().get_uring();
    auto token = uring.submit(
        ioreq::open{
            path,
            uring_openmode,
            perm,
            uring_resolvemode,
        });

    auto deal_with_res = [&path, opts](int res) -> std::expected<file, file::open_result> {
        if (res < 0)
            return std::unexpected{static_cast<file::open_result>(-res)};

        auto f = file{res, std::move(path), opts};
        if (opts.has(file::options::append)) {
            f.seekg(0, seekpos::end);
            f.seekp(0, seekpos::end);
        }
        return std::move(f);
    };

    if (auto res = uring.peek(token.seq_num))
        co_return deal_with_res(*res);

    auto res = co_await peek_uring(uring, token.seq_num);
    co_return deal_with_res(res);
}

future<void> file::close() {
    if (none)
        co_return;

    none = true;
    auto fh = fhandle;
    if (is_destructor_close.load(morder::acquire))
        destructor_can_exit.test_and_set();
    fhandle = raw_handle_invalid;

    auto &uring = RT::__worker::get_worker().get_uring();
    auto token = uring.submit(ioreq::close{fh});

    if (auto res = uring.peek(token.seq_num))
        co_return;

    (void)co_await peek_uring(uring, token.seq_num);
    co_return;
}

future<std::optional<io::buffer<>>> file::write(buffer<> buf) {
    if (none)
        throw inner_exception("[ASCO] asco::io::file::write(): file not opened");

    if (!opts.has(options::write))
        throw inner_exception("[ASCO] asco::io::file::write(): file not opened with option::write");

    auto &uring = RT::__worker::get_worker().get_uring();
    auto token = uring.submit(ioreq::write{fhandle, std::move(buf), pwrite});

    auto deal_with_res = [&pwrite = this->pwrite, &uring,
                          seq_num = token.seq_num](int res) -> std::optional<io::buffer<>> {
        if (res < 0)
            throw inner_exception(
                std::format("[ASCO] asco::io::file::write(): write failed because: {}", std::strerror(-res)));

        pwrite += res;
        return uring.peek_rest_write_buffer(seq_num, res);
    };

    if (auto res = uring.peek(token.seq_num))
        co_return deal_with_res(*res);

    auto res = co_await peek_uring(uring, token.seq_num);
    co_return deal_with_res(res);
}

future<std::expected<buffer<>, read_result>> file::read(size_t nbytes) {
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

            this_coro::throw_coroutine_abort<future<std::expected<buffer<>, read_result>>>();

            switch (state) {
            case 1: {
                auto val = this_coro::move_back_return_value<future<std::expected<buffer<>, read_result>>>();
                if (val)
                    self.aborted_buffer = std::move(*val);
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

    token = uring->submit(ioreq::read{fhandle, nbytes, pread});

after_submit:

    auto deal_with_res = [&restorer, &pread = this->pread, &uring,
                          seq_num = token.seq_num](int res) -> std::expected<buffer<>, read_result> {
        if (res == 0)
            return std::unexpected{read_result::eof};
        else if (res < 0) {
            switch (-res) {
            case EINTR:
                return std::unexpected{read_result::interrupted};
            case EAGAIN:
                return std::unexpected{read_result::again};
            default:
                throw inner_exception(
                    std::format(
                        "[ASCO] asco::io::file::read(): read failed because: {}", std::strerror(-res)));
            }
        } else if (auto rbuf = uring->peek_read_buffer(seq_num, res)) {
            restorer.state = 1;
            restorer.pread_inc = res;
            pread += res;
            return std::move(*rbuf);
        } else {
            throw inner_exception("[ASCO] asco::io::file::read(): cqe peeked but read buffer not found");
        }
    };

    if (auto res = uring->peek(token.seq_num))
        co_return deal_with_res(*res);

    if (this_coro::aborted()) {
        aborted_token = token;
        throw base::coroutine_abort{};
    }

    auto res = co_await peek_uring(*uring, token.seq_num);

    if (this_coro::aborted()) {
        aborted_token = token;
        throw base::coroutine_abort{};
    }

    co_return deal_with_res(res);
}

};  // namespace asco::io
