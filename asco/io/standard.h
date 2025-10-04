// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_IO_STANDARD_H
#define ASCO_IO_STANDARD_H 1

#include <asco/compile_time/platform.h>
#include <asco/future.h>
#include <asco/io/bufio.h>
#include <asco/io/file.h>
#include <asco/sync/mutex.h>

// I don't like these macros defined in C89/C99 standard headers. They polluted all the program.
// Compilers don't care if you use these bullshit even though you see them as macros, they are also global
// variables.
#undef stdin
#undef stdout
#undef stderr

namespace asco::io {

class stdin_ {
public:
    stdin_(const stdin_ &) = delete;
    stdin_(stdin_ &&) = delete;

    stdin_ &operator=(const stdin_ &) = delete;
    stdin_ &operator=(stdin_ &&) = delete;

    static stdin_ &get();

    future_inline<buffer<>> read(size_t nbytes);

    future_inline<buffer<>> read_all();

    generator<std::string> read_lines(newline nl = [] consteval {
        using compile_time::platform::platform;
        using compile_time::platform::os;
        if constexpr (platform::os_is(os::linux))
            return newline::lf;
        else if constexpr (platform::os_is(os::windows))
            return newline::crlf;
        else if constexpr (platform::os_is(os::apple))
            return newline::cr;
    }());

private:
    stdin_(file &&f);

    stream_reader<file> base;
    mutex<> mtx{};
};

inline stdin_ &stdin() { return stdin_::get(); }

class stdout_ {
public:
    stdout_(const stdout_ &) = delete;
    stdout_(stdout_ &&) = delete;

    stdout_ &operator=(const stdout_ &) = delete;
    stdout_ &operator=(stdout_ &&) = delete;

    static stdout_ &get();

    future_inline<void> write(buffer<> buf);

    future_inline<void> write_all(buffer<> buf);

    future_inline<void> flush();

private:
    stdout_(file &&f);

    stream_writer<file> base;
    mutex<> mtx{};
};

inline stdout_ &stdout() { return stdout_::get(); }

class stderr_ {
public:
    stderr_(const stderr_ &) = delete;
    stderr_(stderr_ &&) = delete;

    stderr_ &operator=(const stderr_ &) = delete;
    stderr_ &operator=(stderr_ &&) = delete;

    static stderr_ &get();

    future_inline<void> write(buffer<> buf);

private:
    stderr_(file &&f);

    stream_writer<file> base;
    mutex<> mtx{};
};

inline stderr_ &stderr() { return stderr_::get(); }

};  // namespace asco::io

namespace asco {

using io::stdin, io::stdout, io::stderr;

};

#endif
