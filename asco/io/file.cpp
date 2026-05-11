// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/io/file.h>

#include <asco/core/runtime.h>
#include <asco/core/worker.h>
#include <asco/yield.h>

namespace asco::io {

future<std::expected<file, open_failed>> file_open::open(this file_open self) {
    auto request = core::os::io_req::open{self.m_path, self.m_flags, self.m_modes};
    auto &io = core::runtime::current().get_io_adapter();
    core::awake_token token{};
    auto reqid = io.submit_open(request, core::awake_token{token});
    co_await this_task::yield();
    auto res = io.complete_open(reqid);
    if (res) {
        co_return file(*res);
    } else {
        co_return std::unexpected{res.error()};
    }
}

file_open file::at(std::string_view path) { return file_open{path, false}; }
file_open file::at(std::filesystem::path path) { return file_open{path.string(), false}; }

file_open file::create(std::string_view path) { return file_open{path, true}; }
file_open file::create(std::filesystem::path path) { return file_open{path.string(), true}; }

file::file(file &&rhs) noexcept
        : m_handle{rhs.m_handle}
        , m_valid{rhs.m_valid}
        , m_fptr{rhs.m_fptr} {
    rhs.m_valid = false;
}

file &file::operator=(file &&rhs) noexcept {
    if (this != &rhs) {
        close();

        m_handle = rhs.m_handle;
        m_valid = rhs.m_valid;
        m_fptr = rhs.m_fptr;
        rhs.m_valid = false;
    }
    return *this;
}

future<std::expected<void, open_failed>>
file::open(std::string path, util::flags<open_flags> flags, util::flags<create_mode> mode) {
    if (m_valid) {
        close();
    }

    auto request = core::os::io_req::open{path, flags, mode};
    auto &io = core::runtime::current().get_io_adapter();
    core::awake_token token{};
    auto reqid = io.submit_open(request, core::awake_token{token});
    co_await this_task::yield();
    auto res = io.complete_open(reqid);
    if (res) {
        m_handle = *res;
        m_valid = true;
        m_fptr = 0;
        co_return std::expected<void, open_failed>{};
    } else {
        co_return std::unexpected{res.error()};
    }
}

future<std::expected<void, open_failed>>
file::open(std::string_view path, util::flags<open_flags> flags, util::flags<create_mode> mode) {
    return open(std::string{path}, flags, mode);
}

future<std::expected<void, open_failed>>
file::open(std::filesystem::path path, util::flags<open_flags> flags, util::flags<create_mode> mode) {
    return open(std::string{path.string()}, flags, mode);
}

void file::close() {
    if (m_valid) {
        auto _ = spawn([handle = m_handle] -> future<void> {
            auto request = core::os::io_req::close{handle};
            auto &io = core::runtime::current().get_io_adapter();
            core::awake_token token{};
            auto reqid = io.submit_close(request, core::awake_token{token});
            co_await this_task::yield();
            io.complete_close(reqid);
            co_return;
        });
        m_valid = false;
    }
}

void file::seek(std::ptrdiff_t offset, seek_mode mode) {
    check_valid();

    switch (mode) {
    case seek_mode::set: {
        if (offset < 0) [[unlikely]] {
            m_fptr = 0;
        } else {
            m_fptr = offset;
        }
    } break;
    case seek_mode::current: {
        if (offset < 0 && -offset > m_fptr) [[unlikely]] {
            m_fptr = 0;
        } else {
            m_fptr += offset;
        }
    } break;
    case seek_mode::end: {
        if (offset > 0) [[unlikely]] {
            m_fptr = 0;
        } else {
            m_fptr = offset;
        }
        auto &io = core::runtime::current().get_io_adapter();
        core::os::io_state state = io.get_state(m_handle);
        if (-m_fptr > static_cast<std::ptrdiff_t>(state.size)) {
            m_fptr = 0;
        } else {
            m_fptr += static_cast<std::ptrdiff_t>(state.size);
        }
    } break;
    }
}

future<std::expected<io::buffer<>, read_failed>> file::read(std::size_t size) {
    check_valid();

    auto buf = io::buffer{size};
    auto read_size = buf.try_advance(size);
    auto request = core::os::io_req::read{m_handle, static_cast<std::size_t>(m_fptr), buf, read_size};
    auto &io = core::runtime::current().get_io_adapter();
    core::awake_token token{};
    auto reqid = io.submit_read(request, core::awake_token{token});
    co_await this_task::yield();
    auto res = io.complete_read(reqid);
    if (res) {
        buf.m_cursor = *res;
        m_fptr += *res;
        co_return std::move(buf);
    } else {
        co_return std::unexpected{res.error()};
    }
}

future<std::expected<std::size_t, write_failed>> file::write(const buffer<> &buf) {
    check_valid();

    auto request = core::os::io_req::write{m_handle, static_cast<std::size_t>(m_fptr), buf};
    auto &io = core::runtime::current().get_io_adapter();
    core::awake_token token{};
    auto reqid = io.submit_write(request, core::awake_token{token});
    co_await this_task::yield();
    auto res = io.complete_write(reqid);
    if (res) {
        m_fptr += *res;
        co_return *res;
    } else {
        co_return std::unexpected{res.error()};
    }
}

};  // namespace asco::io
