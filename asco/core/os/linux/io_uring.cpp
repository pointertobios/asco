// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/os/io.h>

#include <chrono>
#include <fcntl.h>
#include <liburing.h>
#include <ranges>
#include <tuple>

#include <asco/concurrency/hash_map.h>
#include <asco/core/daemon.h>
#include <asco/core/mm/pool.h>
#include <asco/panic.h>

namespace asco::core::os {

io_handle io_handle_from_unix_fd(int fd) { return static_cast<io_handle>(fd); }

io_handle io_handle_from_windows_handle(void *) {
    panic("io_handle_from_windows_handle: Linux 平台未实现 Windows 句柄到 io_handle 的转换");
}

int fd_from_io_handle(io_handle handle) { return static_cast<int>(handle); }

class io_uring_impl : public io_adapter, protected daemon {
public:
    io_uring_impl()
            : io_adapter{util::erased{util::erased::ref{*this}}}
            , daemon{"asco::io"} {
        ::io_uring_params p{};
        p.flags = IORING_SETUP_CQSIZE | IORING_SETUP_SQPOLL | IORING_SETUP_CLAMP;
        p.cq_entries = 1024;
        auto ret = ::io_uring_queue_init_params(256, &m_ring, &p);
        if (ret < 0) {
            panic("io_uring_queue_init_params: {}", std::error_code{-ret, std::generic_category()}.message());
        }
        daemon::register_awake_hook([this] {
            ::io_uring_sqe *sqe{nullptr};
            while (!sqe) {
                sqe = ::io_uring_get_sqe(&m_ring);
            }
            ::io_uring_prep_nop(sqe);
            auto ret = ::io_uring_submit(&m_ring);
            if (ret < 0) {
                panic("io_uring_submit: {}", std::error_code{-ret, std::generic_category()}.message());
            }
        });
        auto _ = daemon::start();
    }

    ~io_uring_impl() {
        daemon::join();
        ::io_uring_queue_exit(&m_ring);
    }

    ::io_uring_sqe *get_sqe() {
        ::io_uring_sqe *sqe{nullptr};
        do {
            sqe = ::io_uring_get_sqe(&m_ring);
        } while (!sqe);
        return sqe;
    }

    io_request_id
    submit(io_request auto &request, awake_token &&token, std::invocable<::io_uring_sqe *> auto &&prep_fn) {
        auto reqid = request.gen_request_id();
        m_requests.insert(reqid, {std::move(token), {}});
        ::io_uring_sqe *sqe = get_sqe();
        prep_fn(sqe);
        ::io_uring_sqe_set_data64(sqe, reqid);
        if (::io_uring_submit(&m_ring) < 0) {
            panic("io_uring_submit: 请求 {:#016x} 提交失败", reqid);
        }
        return reqid;
    }

    int complete(io_request_id reqid) {
        if (auto req = m_requests.remove(reqid)) {
            auto &[_, res] = *req;
            return res;
        } else {
            panic("io_uring_impl: 未完成的请求 {:#016x}", reqid);
        }
    }

    io_request_id submit_open(io_req::open &request, awake_token &&token) {
        auto &path = *new (request.path_cstr.get()) mm::cstring{request.path};
        request.using_path_cstr = true;
        int flags = 0;
        if (request.flags.contains(open_flags::read) && request.flags.contains(open_flags::write)) {
            flags |= O_RDWR;
        } else if (request.flags.contains(open_flags::write)) {
            flags |= O_WRONLY;
        } else {
            flags |= O_RDONLY;
        }
        if (request.flags.contains(open_flags::create)) {
            flags |= O_CREAT;
        }
        if (request.flags.contains(open_flags::truncate)) {
            flags |= O_TRUNC;
        }
        if (request.flags.contains(open_flags::append)) {
            flags |= O_APPEND;
        }
        if (request.flags.contains(open_flags::exclusive)) {
            flags |= O_EXCL;
        }
        if (request.flags.contains(open_flags::temporary)) {
            flags |= O_TMPFILE;
        }
        ::mode_t mode = 0;
        if (request.modes.contains(create_mode::read)) {
            mode |= 1 << 8;
        }
        if (request.modes.contains(create_mode::write)) {
            mode |= 1 << 7;
        }
        if (request.modes.contains(create_mode::execute)) {
            mode |= 1 << 6;
        }
        if (request.modes.contains(create_mode::read_share)) {
            mode |= 9 << 2;
        }
        if (request.modes.contains(create_mode::write_share)) {
            mode |= 9 << 1;
        }
        if (request.modes.contains(create_mode::execute_share)) {
            mode |= 9;
        }

        return submit(
            request, std::move(token),
            [path = static_cast<const char *>(path), flags, mode](::io_uring_sqe *sqe) {
                ::io_uring_prep_openat(sqe, AT_FDCWD, path, flags, mode);
            });
    }

    std::expected<io_handle, open_failed> complete_open(io_request_id reqid) {
        auto res = complete(reqid);

        if (res < 0) {
            auto type = [&]() {
                switch (-res) {
                case ENOENT:
                    return open_failed::not_found;
                case EACCES:
                    return open_failed::permission_denied;
                case EEXIST:
                    return open_failed::already_exists;
                case ENAMETOOLONG:
                case ENOTDIR:
                case EISDIR:
                    return open_failed::invalid_path;
                case EMFILE:
                case ENFILE:
                    return open_failed::too_many_open_files;
                default:
                    return open_failed::other;
                }
            }();
            return std::unexpected{open_failed{type, res}};
        } else {
            return io_handle_from_unix_fd(res);
        }
    }

    io_request_id submit_close(io_req::close &request, awake_token &&token) {
        int fd = fd_from_io_handle(request.handle);

        return submit(
            request, std::move(token), [fd](::io_uring_sqe *sqe) { ::io_uring_prep_close(sqe, fd); });
    }

    void complete_close(io_request_id reqid) { complete(reqid); }

    io_request_id submit_read(io_req::read &request, awake_token &&token) {
        int fd = fd_from_io_handle(request.handle);
        auto &buf = request.buffer;

        return submit(
            request, std::move(token),
            [fd, &buf, offset = request.offset, size = request.size](::io_uring_sqe *sqe) {
                ::io_uring_prep_read(sqe, fd, buf.data(), size, offset);
            });
    }

    std::expected<std::size_t, read_failed> complete_read(io_request_id reqid) {
        int res = complete(reqid);
        if (res < 0) {
            auto type = [&]() {
                switch (-res) {
                case EBADF:
                    return read_failed::invalid_handle;
                case EFAULT:
                    return read_failed::bad_buffer;
                case EACCES:
                    return read_failed::permission_denied;
                default:
                    return read_failed::other;
                }
            }();
            return std::unexpected{read_failed{type, res}};
        } else {
            return static_cast<std::size_t>(res);
        }
    }

    io_request_id submit_write(io_req::write &request, awake_token &&token) {
        int fd = fd_from_io_handle(request.handle);
        auto &buf = request.buffer;

        return submit(request, std::move(token), [fd, &buf, offset = request.offset](::io_uring_sqe *sqe) {
            ::io_uring_prep_write(sqe, fd, buf.data(), buf.cursor(), offset);
        });
    }

    std::expected<std::size_t, write_failed> complete_write(io_request_id reqid) {
        int res = complete(reqid);
        if (res < 0) {
            auto type = [&]() {
                switch (-res) {
                case EBADF:
                    return write_failed::invalid_handle;
                case EFAULT:
                    return write_failed::bad_buffer;
                case EACCES:
                    return write_failed::permission_denied;
                default:
                    return write_failed::other;
                }
            }();
            return std::unexpected{write_failed{type, res}};
        } else {
            return static_cast<std::size_t>(res);
        }
    }

private:
    ::io_uring m_ring;

    concurrency::hash_map<io_request_id, std::tuple<awake_token, int>> m_requests;

    bool init() override { return true; }

    bool run_once(std::stop_token &st) override {
        ::io_uring_cqe *cqe{nullptr};
        auto ret = ::io_uring_wait_cqe(&m_ring, &cqe);
        if (ret < 0) {
            panic("io_uring_wait_cqe: {}", std::error_code{-ret, std::generic_category()}.message());
        }
        auto reqid = ::io_uring_cqe_get_data64(cqe);
        if (st.stop_requested()) {
            return false;
        }
        if (auto g = m_requests.get(reqid)) {
            auto &[token, res] = g.value();
            res = cqe->res;
            token.awake();
        } else {
            panic("io_uring_impl: 收到未知请求完成 {:#016x}，结果 {}", reqid, cqe->res);
        }
        ::io_uring_cqe_seen(&m_ring, cqe);
        return true;
    }

    void shutdown() override {}
};

std::unique_ptr<io_adapter> io_adapter::create() { return std::make_unique<io_uring_impl>(); }

io_state io_adapter::get_state(io_handle handle) {
    int fd = fd_from_io_handle(handle);
    struct stat st;
    if (::fstat(fd, &st) < 0) {
        panic(
            "io_adapter::get_state: fstat 失败，句柄 {}, 错误 {}", handle,
            std::error_code{errno, std::generic_category()}.message());
    }
    return io_state{
        static_cast<std::size_t>(st.st_size),
        std::chrono::system_clock::time_point(std::chrono::seconds(st.st_atime)),
        std::chrono::system_clock::time_point(std::chrono::seconds(st.st_mtime)),
        std::chrono::system_clock::time_point(std::chrono::seconds(st.st_ctime)),
        static_cast<std::size_t>(st.st_uid)};
}

io_request_id io_adapter::submit_open(io_req::open &request, awake_token &&token) {
    return m_impl.get<io_uring_impl>().submit_open(request, std::move(token));
}

std::expected<io_handle, open_failed> io_adapter::complete_open(io_request_id reqid) {
    return m_impl.get<io_uring_impl>().complete_open(reqid);
}

io_request_id io_adapter::submit_close(io_req::close &request, awake_token &&token) {
    return m_impl.get<io_uring_impl>().submit_close(request, std::move(token));
}

void io_adapter::complete_close(io_request_id reqid) { m_impl.get<io_uring_impl>().complete_close(reqid); }

io_request_id io_adapter::submit_read(io_req::read &request, awake_token &&token) {
    return m_impl.get<io_uring_impl>().submit_read(request, std::move(token));
}

std::expected<std::size_t, read_failed> io_adapter::complete_read(io_request_id reqid) {
    return m_impl.get<io_uring_impl>().complete_read(reqid);
}

io_request_id io_adapter::submit_write(io_req::write &request, awake_token &&token) {
    return m_impl.get<io_uring_impl>().submit_write(request, std::move(token));
}

std::expected<std::size_t, write_failed> io_adapter::complete_write(io_request_id reqid) {
    return m_impl.get<io_uring_impl>().complete_write(reqid);
}

};  // namespace asco::core::os
