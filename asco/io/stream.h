// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_IO_STREAM_H
#define ASCO_IO_STREAM_H 1

#include <asco/future.h>
#include <asco/futures.h>
#include <asco/io/buffer.h>
#include <asco/utils/pubusing.h>

#include <fstream>

namespace asco::io {

using namespace types;
using namespace literals;

template<typename CharT>
    requires std::is_integral_v<CharT> || std::is_same_v<CharT, char> || std::is_same_v<CharT, std::byte>
class stream_base {
public:
    friend class buffer<CharT>;

    ~stream_base() {
        if (!closed)
            close();
    }

    virtual future<size_t> read(buffer<CharT> &buf, size_t size) = 0;
    virtual future_void write(buffer<CharT> &&buf) = 0;

    virtual future_void close() = 0;

private:
    bool closed{false};
};

template<typename CharT = char>
    requires std::is_integral_v<CharT> || std::is_same_v<CharT, char> || std::is_same_v<CharT, std::byte>
class fstream : public stream_base<CharT> {
public:
private:
    fstream(std::string_view path)
            : file(path, std::ios::in | std::ios::out | std::ios::binary) {}

    future<size_t> read(buffer<CharT> &buf, size_t size) override {
        buf.clear();

        size_t buffer_size;
        if (size <= 1_MB)
            buffer_size = 4_KB;
        if (size <= 512_MB)
            buffer_size = 2_MB;
        else
            buffer_frame = 1_GB;

        size_t read_size{0};
        while (read_size < size) {
            size_t current_buffer_size = std::min(size - read_size, buffer_size);
            auto buffer = new CharT[current_buffer_size];
            size_t read = file.read(buffer, current_buffer_size);
            read_size += read;
            if (read)
                buf.push_raw_array_buffer(buffer, current_buffer_size, read);
            if (read < current_buffer_size || file.eof())
                break;
        }
        if (read_size > size)
            throw asco::inner_exception("istream::read(): Unexpected read size that out of acquired size.");

        co_return read_size;
    }

    future_void write(buffer<CharT> &&buf) override {}

    future_void close() override {
        file.close();
        closed = true;
    }

private:
    std::basic_fstream<CharT> file;
};

};  // namespace asco::io

#endif
