// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/compile_time/platform.h>
#include <asco/io/standard.h>

namespace asco::io {

stdin_::stdin_(file &&f)
        : base{std::move(f)} {}

stdin_ &stdin_::get() {
    static stdin_ instance{file{
        [] consteval -> file::file_raw_handle {
            using compile_time::platform::platform;
            using compile_time::platform::os;
            if constexpr (platform::os_is(os::linux) || platform::os_is(os::apple))
                return 0;
        }(),
        file::options::read}};
    return instance;
}

future_inline<buffer<>> stdin_::read(size_t nbytes) {
    auto g = co_await mtx.lock();
    co_return co_await base.read(nbytes);
}

future_inline<buffer<>> stdin_::read_all() {
    auto g = co_await mtx.lock();
    co_return co_await base.read_all();
}

generator<std::string> stdin_::read_lines(newline nl) { return base.read_lines(nl, mtx.lock()); }

stdout_::stdout_(file &&f)
        : base{std::move(f)} {}

stdout_ &stdout_::get() {
    static stdout_ instance{file{1, file::options::write}};
    return instance;
}

future_inline<void> stdout_::write(buffer<> buf) {
    auto g = co_await mtx.lock();
    co_return co_await base.write(std::move(buf));
}

future_inline<void> stdout_::write_all(buffer<> buf) {
    auto g = co_await mtx.lock();
    co_return co_await base.write_all(std::move(buf));
}

future_inline<void> stdout_::flush() {
    auto g = co_await mtx.lock();
    co_return co_await base.flush();
}

stderr_::stderr_(file &&f)
        : base{std::move(f)} {}

stderr_ &stderr_::get() {
    static stderr_ instance{file{2, file::options::write}};
    return instance;
}

future_inline<void> stderr_::write(buffer<> buf) {
    auto g = co_await mtx.lock();
    co_return co_await base.write_all(std::move(buf));
}
};  // namespace asco::io
