// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/os/io.h>

#include <chrono>
#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>

#ifndef NOMINMAX
#    define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#include <aclapi.h>
#include <sddl.h>
#include <windows.h>

#include <asco/concurrency/hash_map.h>
#include <asco/core/mm/pool.h>
#include <asco/core/worker.h>
#include <asco/panic.h>
#include <asco/util/murmur.h>

namespace asco::core::os {

io_handle io_handle_from_unix_fd(int) {
    panic("io_handle_from_unix_fd: Windows 平台未实现 Unix 文件描述符到 io_handle 的转换");
}

io_handle io_handle_from_windows_handle(void *handle) { return reinterpret_cast<io_handle>(handle); }

HANDLE handle_from_io_handle(io_handle handle) { return reinterpret_cast<HANDLE>(handle); }

static std::string gen_sddl(util::flags<create_mode> mode) {
    std::string sddl = "D:";
    if (mode.contains(create_mode::read)) {
        sddl += "(A;;FR;;;OW)";
    }
    if (mode.contains(create_mode::write)) {
        sddl += "(A;;FW;;;OW)";
    }
    if (mode.contains(create_mode::execute)) {
        sddl += "(A;;FX;;;OW)";
    }
    if (mode.contains(create_mode::read_share)) {
        sddl += "(A;;FR;;;WD)";
    }
    if (mode.contains(create_mode::write_share)) {
        sddl += "(A;;FW;;;WD)";
    }
    if (mode.contains(create_mode::execute_share)) {
        sddl += "(A;;FX;;;WD)";
    }
    return sddl;
}

class iocp_impl : public io_adapter, protected daemon {
    struct operation {
        OVERLAPPED overlapped;
        io_request_id reqid;
    };

    struct request_wrap {
        operation op;
        core::awake_token token;
        bool finished{false};
        BOOL ok{};
        DWORD bytes_transferred{};
        DWORD error_code{};
    };

public:
    iocp_impl()
            : io_adapter{util::erased{util::erased::ref{*this}}}
            , daemon{"asco::io"} {
        m_iocp = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
        if (!m_iocp) {
            panic(
                "iocp_impl: CreateIoCompletionPort 失败，错误 {}",
                std::error_code{static_cast<int>(::GetLastError()), std::system_category()}.message());
        }
        daemon::register_awake_hook([this] { ::PostQueuedCompletionStatus(m_iocp, 0, 0, nullptr); });
        auto _ = daemon::start();
    }

    ~iocp_impl() {
        daemon::join();
        ::CloseHandle(m_iocp);
    }

    io_request_id submit_open(io_req::open &request, awake_token &&) {
        io_request_id reqid = request.gen_request_id();
        auto &flags = request.flags;

        auto &path = *new (request.path_cstr.get()) mm::cstring{request.path};
        request.using_path_cstr = true;

        DWORD access = 0;
        if (flags.contains(open_flags::read)) {
            access |= GENERIC_READ;
        }
        if (flags.contains(open_flags::write)) {
            access |= GENERIC_WRITE;
        }
        if (flags.contains(open_flags::append)) {
            access |= FILE_APPEND_DATA;
        }
        if (flags.contains(open_flags::temporary)) {
            access |= DELETE;
        }

        DWORD temporary = 0;
        if (flags.contains(open_flags::temporary)) {
            temporary = FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE;
        }

        PSECURITY_DESCRIPTOR sd = nullptr;
        if (!::ConvertStringSecurityDescriptorToSecurityDescriptorA(
                gen_sddl(request.modes).c_str(), SDDL_REVISION_1, &sd, nullptr)) {
            panic(
                "iocp_impl: ConvertStringSecurityDescriptorToSecurityDescriptorA 失败，错误 {}",
                std::error_code{static_cast<int>(::GetLastError()), std::system_category()}.message());
        }
        SECURITY_ATTRIBUTES security_attr{};
        security_attr.nLength = sizeof(security_attr);
        security_attr.lpSecurityDescriptor = sd;
        security_attr.bInheritHandle = true;

        DWORD creation = 0;
        if (!flags.contains(open_flags::create) && !flags.contains(open_flags::truncate)) {
            creation = OPEN_EXISTING;
        } else if (
            flags.contains(open_flags::create) && !flags.contains(open_flags::exclusive)
            && !flags.contains(open_flags::truncate)) {
            creation = OPEN_ALWAYS;
        } else if (flags.contains(open_flags::create) && !flags.contains(open_flags::truncate)) {
            creation = CREATE_NEW;
        } else if (
            flags.contains(open_flags::create) && !flags.contains(open_flags::exclusive)
            && flags.contains(open_flags::truncate)) {
            creation = CREATE_ALWAYS;
        } else if (
            !flags.contains(open_flags::create) && !flags.contains(open_flags::exclusive)
            && flags.contains(open_flags::truncate)) {
            creation = TRUNCATE_EXISTING;
        }

        HANDLE fhandle = ::CreateFileA(
            path, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &security_attr, creation,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED | temporary, nullptr);

        if (fhandle == INVALID_HANDLE_VALUE) {
            auto type = [&]() {
                switch (::GetLastError()) {
                case ERROR_FILE_NOT_FOUND:
                    return open_failed::not_found;
                case ERROR_PATH_NOT_FOUND:
                    return open_failed::not_found;
                case ERROR_ACCESS_DENIED:
                    return open_failed::permission_denied;
                case ERROR_FILE_EXISTS:
                    return open_failed::already_exists;
                case ERROR_INVALID_NAME:
                    return open_failed::invalid_path;
                case ERROR_TOO_MANY_OPEN_FILES:
                    return open_failed::too_many_open_files;
                default:
                    return open_failed::other;
                }
            }();
            m_open_results.insert(
                reqid, std::unexpected{open_failed{type, static_cast<int>(::GetLastError())}});
        } else {
            io_handle handle = io_handle_from_windows_handle(fhandle);
            m_open_results.insert(reqid, handle);
            if (!::CreateIoCompletionPort(fhandle, m_iocp, 0, 0)) {
                panic(
                    "iocp_impl: CreateIoCompletionPort 失败，句柄 {}, 错误 {}", handle,
                    std::error_code{static_cast<int>(::GetLastError()), std::system_category()}.message());
            }
        }

        if (sd) {
            ::LocalFree(sd);
        }

        return reqid;
    }

    std::expected<io_handle, open_failed> complete_open(io_request_id reqid) {
        if (auto res = m_open_results.remove(reqid)) {
            return *res;
        } else {
            panic("iocp_impl::complete_open: 未找到请求 ID {} 的结果", reqid);
        }
    }

    io_request_id submit_close(io_req::close &request, awake_token &&) {
        HANDLE handle = handle_from_io_handle(request.handle);
        if (!::CloseHandle(handle)) {
            panic(
                "iocp_impl::submit_close: CloseHandle 失败，句柄 {}, 错误 {}", request.handle,
                std::error_code{static_cast<int>(::GetLastError()), std::system_category()}.message());
        }
        return request.gen_request_id();
    }

    void complete_close(io_request_id) {}

    io_request_id
    submit(io_request auto &request, awake_token &&token, std::invocable<request_wrap &> auto &&prep_fn)
        requires(std::is_convertible_v<std::invoke_result_t<decltype(prep_fn), request_wrap &>, bool>)
    {
        io_request_id reqid = request.gen_request_id();
        auto reqw = mm::pmr::get<request_wrap>().allocate_object<request_wrap>();
        new (reqw) request_wrap{{{}, reqid}, std::move(token)};
        if (!m_requests.insert(reqid, reqw)) {
            panic("iocp_impl::submit: 请求 ID {} 已存在", reqid);
        }
        token.suspend();
        if (!prep_fn(*reqw)) {
            ::PostQueuedCompletionStatus(m_iocp, 0, 0, &reqw->op.overlapped);
        }
        return reqid;
    }

    request_wrap complete(io_request_id reqid) {
        if (auto res = m_requests.remove(reqid)) {
            auto reqw = *res;
            request_wrap result = std::move(*reqw);
            mm::pmr::get<request_wrap>().deallocate_object(reqw);
            return result;
        } else {
            panic("iocp_impl::complete: 未找到请求 ID {} 的结果", reqid);
        }
    }

    io_request_id submit_read(io_req::read &request, awake_token &&token) {
        HANDLE handle = handle_from_io_handle(request.handle);
        auto &buf = request.buffer;

        return submit(
            request, std::move(token),
            [handle, &buf, offset = request.offset, size = request.size](request_wrap &reqw) {
                auto &req = reqw.op;
                req.overlapped.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
                req.overlapped.OffsetHigh = static_cast<DWORD>((offset >> 32) & 0xFFFFFFFF);
                if (!::ReadFile(handle, buf.data(), static_cast<DWORD>(size), nullptr, &req.overlapped)) {
                    if (auto err = ::GetLastError(); err != ERROR_IO_PENDING) {
                        reqw.finished = true;
                        reqw.ok = err == ERROR_HANDLE_EOF;
                        reqw.bytes_transferred = 0;
                        reqw.error_code = err;
                        return false;
                    }
                }
                return true;
            });
    }

    std::expected<std::size_t, read_failed> complete_read(io_request_id reqid) {
        auto reqw = complete(reqid);
        if (!reqw.ok) {
            if (reqw.error_code == ERROR_HANDLE_EOF) {
                return 0;
            }
            auto type = [&]() {
                switch (reqw.error_code) {
                case ERROR_INVALID_HANDLE:
                    return read_failed::invalid_handle;
                case ERROR_NOACCESS:
                    return read_failed::bad_buffer;
                case ERROR_ACCESS_DENIED:
                    return read_failed::permission_denied;
                default:
                    return read_failed::other;
                }
            }();
            return std::unexpected{read_failed{type, reqw.error_code}};
        } else {
            return static_cast<std::size_t>(reqw.bytes_transferred);
        }
    }

    io_request_id submit_write(io_req::write &request, awake_token &&token) {
        HANDLE handle = handle_from_io_handle(request.handle);
        auto &buf = request.buffer;

        return submit(request, std::move(token), [handle, &buf, offset = request.offset](request_wrap &reqw) {
            auto &req = reqw.op;
            req.overlapped.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
            req.overlapped.OffsetHigh = static_cast<DWORD>((offset >> 32) & 0xFFFFFFFF);
            if (!::WriteFile(
                    handle, buf.data(), static_cast<DWORD>(buf.cursor()), nullptr, &req.overlapped)) {
                if (auto err = ::GetLastError(); err != ERROR_IO_PENDING) {
                    reqw.finished = true;
                    reqw.ok = err == ERROR_HANDLE_EOF;
                    reqw.bytes_transferred = 0;
                    reqw.error_code = err;
                    return false;
                }
            }
            return true;
        });
    }

    std::expected<std::size_t, write_failed> complete_write(io_request_id reqid) {
        auto reqw = complete(reqid);
        if (!reqw.ok) {
            auto type = [&]() {
                switch (reqw.error_code) {
                case ERROR_INVALID_HANDLE:
                    return write_failed::invalid_handle;
                case ERROR_NOACCESS:
                    return write_failed::bad_buffer;
                case ERROR_ACCESS_DENIED:
                    return write_failed::permission_denied;
                default:
                    return write_failed::other;
                }
            }();
            return std::unexpected{write_failed{type, reqw.error_code}};
        } else {
            return static_cast<std::size_t>(reqw.bytes_transferred);
        }
    }

private:
    HANDLE m_iocp{};

    concurrency::hash_map<io_request_id, std::expected<io_handle, open_failed>> m_open_results;

    concurrency::hash_map<io_request_id, request_wrap *> m_requests;

    bool run_once(std::stop_token &st) override {
        DWORD bytes_transferred;
        ULONG_PTR completion_key;
        LPOVERLAPPED overlapped;
        BOOL ok =
            ::GetQueuedCompletionStatus(m_iocp, &bytes_transferred, &completion_key, &overlapped, INFINITE);
        if (st.stop_requested()) {
            return false;
        }
        if (!overlapped) {
            return true;
        }
        auto reqid = reinterpret_cast<operation *>(overlapped)->reqid;
        if (auto g = m_requests.get(reqid)) {
            auto &reqw = *g.value();
            if (!reqw.finished) {
                reqw.ok = ok;
                reqw.bytes_transferred = bytes_transferred;
                reqw.error_code = ok ? 0 : ::GetLastError();
            }
            reqw.token.awake();
        } else {
            panic(
                "iocp_impl: 收到未知请求完成 {}, 结果 {}", reqid,
                ok ? "成功"
                   : std::error_code{static_cast<int>(::GetLastError()), std::system_category()}.message());
        }
        return true;
    }
};

std::unique_ptr<io_adapter> io_adapter::create() { return std::make_unique<iocp_impl>(); }

static concurrency::hash_map<std::size_t, std::tuple<PSECURITY_DESCRIPTOR, PSID, std::size_t>> _sid_map;
static concurrency::hash_map<HANDLE, std::size_t> _handle_sid_map;

io_state io_adapter::get_state(io_handle handle) {
    HANDLE h = handle_from_io_handle(handle);
    BY_HANDLE_FILE_INFORMATION info;
    if (!::GetFileInformationByHandle(h, &info)) {
        panic(
            "io_adapter::get_state: GetFileInformationByHandle 失败，句柄 {}, 错误 {}", handle,
            std::error_code{static_cast<int>(::GetLastError()), std::system_category()}.message());
    }
    LARGE_INTEGER size;
    size.HighPart = info.nFileSizeHigh;
    size.LowPart = info.nFileSizeLow;

    auto filetime_to_system_clock = [](FILETIME filetime) {
        ULARGE_INTEGER value{};
        value.LowPart = filetime.dwLowDateTime;
        value.HighPart = filetime.dwHighDateTime;

        using filetime_duration = std::chrono::duration<unsigned long long, std::ratio<1, 10'000'000>>;
        constexpr unsigned long long unix_epoch_as_filetime = 116'444'736'000'000'000ULL;
        if (value.QuadPart < unix_epoch_as_filetime) {
            return std::chrono::system_clock::time_point{};
        }

        return std::chrono::system_clock::time_point{
            std::chrono::duration_cast<std::chrono::system_clock::duration>(
                filetime_duration{value.QuadPart - unix_epoch_as_filetime})};
    };

    std::size_t owner_hash;
    if (!_handle_sid_map.contains(h)) {
        PSECURITY_DESCRIPTOR security_info = nullptr;
        PSID owner_sid = nullptr;
        if (ERROR_SUCCESS
            != ::GetSecurityInfo(
                h, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &owner_sid, nullptr, nullptr, nullptr,
                &security_info)) {
            panic(
                "io_adapter::get_state: GetSecurityInfo 失败，句柄 {}, 错误 {}", handle,
                std::error_code{static_cast<int>(::GetLastError()), std::system_category()}.message());
        }
        util::detail::hash_val hash_val = util::detail::hash_str(
            std::string_view{reinterpret_cast<const char *>(owner_sid), ::GetLengthSid(owner_sid)});
        std::size_t hash = hash_val[0] ^ hash_val[1];
        if (!_sid_map.insert(hash, std::make_tuple(security_info, owner_sid, 1))) {
            ::LocalFree(security_info);
            std::get<2>(_sid_map.get(hash).value())++;
        }
        _handle_sid_map.insert(h, hash);
        owner_hash = hash;
    } else {
        owner_hash = _handle_sid_map.get(h).value();
    }

    return io_state{
        static_cast<std::size_t>(size.QuadPart), filetime_to_system_clock(info.ftLastAccessTime),
        filetime_to_system_clock(info.ftLastWriteTime), filetime_to_system_clock(info.ftCreationTime),
        owner_hash};
}

io_request_id io_adapter::submit_open(io_req::open &request, awake_token &&token) {
    return m_impl.get<iocp_impl>().submit_open(request, std::move(token));
}

std::expected<io_handle, open_failed> io_adapter::complete_open(io_request_id reqid) {
    return m_impl.get<iocp_impl>().complete_open(reqid);
}

io_request_id io_adapter::submit_close(io_req::close &request, awake_token &&token) {
    HANDLE handle = handle_from_io_handle(request.handle);
    std::optional<std::size_t> removed = std::nullopt;
    if (auto hash = _handle_sid_map.remove(handle)) {
        if (auto g = _sid_map.get(*hash)) {
            auto &[security_info, _, ref_count] = g.value();
            if (--ref_count == 0) {
                ::LocalFree(security_info);
                removed = *hash;
            }
        }
    }
    if (removed) {
        _sid_map.remove(*removed);
    }
    return m_impl.get<iocp_impl>().submit_close(request, std::move(token));
}

void io_adapter::complete_close(io_request_id reqid) { m_impl.get<iocp_impl>().complete_close(reqid); }

io_request_id io_adapter::submit_read(io_req::read &request, awake_token &&token) {
    return m_impl.get<iocp_impl>().submit_read(request, std::move(token));
}

std::expected<std::size_t, read_failed> io_adapter::complete_read(io_request_id reqid) {
    return m_impl.get<iocp_impl>().complete_read(reqid);
}

io_request_id io_adapter::submit_write(io_req::write &request, awake_token &&token) {
    return m_impl.get<iocp_impl>().submit_write(request, std::move(token));
}

std::expected<std::size_t, write_failed> io_adapter::complete_write(io_request_id reqid) {
    return m_impl.get<iocp_impl>().complete_write(reqid);
}

};  // namespace asco::core::os
