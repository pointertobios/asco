// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include "asco/io/file.h"
#include <asco/compile_time/platform.h>
#include <asco/io/standard.h>

namespace asco::io {

template<typename T>
struct construct_only {
    alignas(alignof(T)) char data[sizeof(T)];
    atomic_bool constructed{false};

    bool test_constructed() const { return constructed.load(morder::acquire); }
    void set_constructed() { constructed.store(true, morder::release); }

    void *address() { return &data; }
    T &instance() { return *std::launder(reinterpret_cast<T *>(address())); }
};

stdin_::stdin_(file &&f)
        : base{std::move(f)} {}

stdin_ &stdin_::get() {
    static construct_only<stdin_> instance_storage;
    if (!instance_storage.test_constructed()) {
        new (instance_storage.address()) stdin_{file{
            [] consteval -> file::file_raw_handle {
                using compile_time::platform::platform;
                using compile_time::platform::os;
                if constexpr (platform::os_is(os::linux) || platform::os_is(os::apple))
                    return 0;
            }(),
            file::options::read}};
        instance_storage.set_constructed();
    }
    return instance_storage.instance();
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
    static construct_only<stdout_> instance_storage;
    if (!instance_storage.test_constructed()) {
        new (instance_storage.address()) stdout_{file{1, file::options::write}};
        instance_storage.set_constructed();
    }
    return instance_storage.instance();
}

future_inline<void> stdout_::write(buffer<> buf) {
    if (buf.empty())
        co_return;

    auto g = co_await mtx.lock();
    co_return co_await base.write(std::move(buf));
}

future_inline<void> stdout_::write_all(buffer<> buf) {
    if (buf.empty())
        co_return;

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
    static construct_only<stderr_> instance_storage;
    if (!instance_storage.test_constructed()) {
        new (instance_storage.address()) stderr_{file{2, file::options::write}};
        instance_storage.set_constructed();
    }
    return instance_storage.instance();
}

future_inline<void> stderr_::write(buffer<> buf) {
    if (buf.empty())
        co_return;

    auto g = co_await mtx.lock();
    co_return co_await base.write_all(std::move(buf));
}
};  // namespace asco::io
