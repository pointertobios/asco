// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <cstddef>
#include <expected>
#include <memory>

#include <asco/core/mm/cstring.h>
#include <asco/io/buffer.h>
#include <asco/sync/condition_variable.h>
#include <asco/util/erased.h>
#include <asco/util/flags.h>
#include <asco/util/raw_storage.h>

namespace asco::core::os {

enum class open_flags {
    read,
    write,
    create,
    truncate,
    append,
    exclusive,
    temporary,
};

enum class create_mode {
    read,
    write,
    execute,
    read_share,
    write_share,
    execute_share,
};

struct open_failed {
    enum {
        not_found,
        permission_denied,
        already_exists,
        invalid_path,
        too_many_open_files,
        other,
    } type;
    std::ptrdiff_t raw_code;
};

struct read_failed {
    enum {
        invalid_handle,
        bad_buffer,
        permission_denied,
        other,
    } type;
    std::ptrdiff_t raw_code;
};

struct write_failed {
    enum {
        invalid_handle,
        bad_buffer,
        permission_denied,
        other,
    } type;
    std::ptrdiff_t raw_code;
};

};  // namespace asco::core::os

namespace asco::util {

template<>
struct flag_enum_traits<core::os::open_flags> {
    using enum core::os::open_flags;
    static constexpr core::os::open_flags values[] = {
        read, write, create, truncate, append, exclusive,
    };
};

template<>
struct flag_enum_traits<core::os::create_mode> {
    using enum core::os::create_mode;
    static constexpr core::os::create_mode values[] = {
        read, write, execute, read_share, write_share, execute_share};
};

};  // namespace asco::util

namespace asco::core::os {

_ASCO_UTIL_FLAGS_ENABLE_BITWISE_OPS(open_flags);
_ASCO_UTIL_FLAGS_ENABLE_BITWISE_OPS(create_mode);

};  // namespace asco::core::os

namespace asco::core::os {

using io_handle = std::size_t;

io_handle io_handle_from_unix_fd(int fd);

io_handle io_handle_from_windows_handle(void *handle);

using io_request_id = std::size_t;

template<typename T>
concept io_request = requires(T t) {
    typename T::io_request;
    { t.gen_request_id() } -> std::convertible_to<io_request_id>;
};

namespace io_req {

#define IO_REQ \
    struct io_request {}

struct open {
    std::string_view path;
    util::flags<open_flags> flags;
    util::flags<create_mode> modes;
    std::chrono::steady_clock::time_point timestamp{std::chrono::steady_clock::now()};

    io_request_id gen_request_id() const {
        return reinterpret_cast<io_request_id>(path.data()) ^ path.size() ^ flags.underlying()
               ^ modes.underlying() ^ timestamp.time_since_epoch().count();
    }

    util::raw_storage<mm::cstring> path_cstr{};
    bool using_path_cstr{false};

    ~open() {
        if (using_path_cstr) {
            path_cstr.get()->~cstring();
        }
    }

    IO_REQ;
};

struct close {
    io_handle handle;

    io_request_id gen_request_id() const { return handle; }

    IO_REQ;
};

struct read {
    io_handle handle;
    std::size_t offset;
    io::buffer<> &buffer;
    std::size_t size;

    io_request_id gen_request_id() const {
        return handle ^ offset ^ reinterpret_cast<io_request_id>(buffer.data()) ^ size;
    }

    IO_REQ;
};

struct write {
    io_handle handle;
    std::size_t offset;
    const io::buffer<> &buffer;

    io_request_id gen_request_id() const {
        return handle ^ reinterpret_cast<io_request_id>(buffer.data()) ^ buffer.size();
    }

    IO_REQ;
};

#undef IO_REQ

};  // namespace io_req

struct io_state {
    std::size_t size;
    std::chrono::system_clock::time_point last_access;
    std::chrono::system_clock::time_point last_modification;
    std::chrono::system_clock::time_point last_status_change;
    std::size_t owner_rawvalue;
};

class io_adapter {
public:
    static std::unique_ptr<io_adapter> create();

    virtual ~io_adapter() = default;

    io_state get_state(io_handle handle);

    io_request_id submit_open(io_req::open &request, awake_token &&token);
    std::expected<io_handle, open_failed> complete_open(io_request_id reqid);

    io_request_id submit_close(io_req::close &request, awake_token &&token);
    void complete_close(io_request_id reqid);

    io_request_id submit_read(io_req::read &request, awake_token &&token);
    std::expected<std::size_t, read_failed> complete_read(io_request_id reqid);

    io_request_id submit_write(io_req::write &request, awake_token &&token);
    std::expected<std::size_t, write_failed> complete_write(io_request_id reqid);

protected:
    io_adapter(util::erased impl)
            : m_impl{std::move(impl)} {}

    util::erased m_impl;
};

};  // namespace asco::core::os
