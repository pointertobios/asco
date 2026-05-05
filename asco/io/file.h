// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

#include <asco/core/os/io.h>
#include <asco/future.h>
#include <asco/io/buffer.h>
#include <asco/panic.h>
#include <asco/util/flags.h>

namespace asco::io {

using open_flags = core::os::open_flags;
using create_mode = core::os::create_mode;
using open_failed = core::os::open_failed;
using read_failed = core::os::read_failed;
using write_failed = core::os::write_failed;

enum class seek_mode {
    set,
    current,
    end,
};

class file_open;

// 非并发的文件对象
class file final {
    friend class file_open;

public:
    static file_open at(std::string_view path);
    static file_open at(std::filesystem::path path);
    static file_open create(std::string_view path);
    static file_open create(std::filesystem::path path);

    static file from_handle(core::os::io_handle handle) { return file(handle); }

    file() = default;

    ~file() { close(); }

    file(const file &) = delete;
    file &operator=(const file &) = delete;

    file(file &&) noexcept;
    file &operator=(file &&) noexcept;

    future<std::expected<void, open_failed>>
    open(std::string path, util::flags<open_flags> flags, util::flags<create_mode> mode);

    future<std::expected<void, open_failed>>
    open(std::string_view path, util::flags<open_flags> flags, util::flags<create_mode> mode);

    future<std::expected<void, open_failed>>
    open(std::filesystem::path path, util::flags<open_flags> flags, util::flags<create_mode> mode);

    void close();

    void seek(std::ptrdiff_t offset, seek_mode mode);

    future<std::expected<io::buffer<>, read_failed>> read(std::size_t size);

    // 需手动保证 buffer 的生命周期
    future<std::expected<std::size_t, write_failed>> write(const buffer<> &buf);

private:
    file(core::os::io_handle handle)
            : m_handle{handle}
            , m_valid{true} {}

    void check_valid() const {
        if (!m_valid) [[unlikely]] {
            panic("file: 无效的文件句柄");
        }
    }

    core::os::io_handle m_handle{};
    bool m_valid{false};

    std::ptrdiff_t m_fptr{0};
};

class file_open final {
    friend class file;

public:
    file_open(const file_open &) = delete;
    file_open &operator=(const file_open &) = delete;

    file_open(file_open &&) = default;
    file_open &operator=(file_open &&) = default;

    file_open enable_flag(open_flags flag) && {
        m_flags |= flag;
        return std::move(*this);
    }

    file_open disable_flag(open_flags flag) && {
        m_flags -= flag;
        return std::move(*this);
    }

    file_open with_mode(util::flags<create_mode> mode) && {
        m_modes = mode;
        return std::move(*this);
    }

    future<std::expected<file, open_failed>> open(this file_open self);

private:
    file_open(std::string_view path, bool create)
            : m_path{path}
            , m_flags{(create ? util::flags{open_flags::create} : util::flags<open_flags>{}) | open_flags::read}
            , m_modes{create_mode::read | create_mode::write | create_mode::read_share} {}

    std::string m_path;
    util::flags<open_flags> m_flags;
    util::flags<create_mode> m_modes;
};

};  // namespace asco::io
