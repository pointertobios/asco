// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_IO_STREAM_H
#define ASCO_IO_STREAM_H 1

#include <asco/future.h>
#include <asco/io/buffer.h>
#include <asco/utils/pubusing.h>

namespace asco::io {

using namespace types;

template<typename CharT>
    requires std::is_integral_v<CharT> || std::is_same_v<CharT, char> || std::is_same_v<CharT, std::byte>
class stream_base {
public:
    friend class buffer<CharT>;

    ~stream_base() { close(); }

    virtual future<size_t> read(buffer<CharT> &buf, size_t size) = 0;
    virtual future_void write(buffer<CharT> &&buf) = 0;

    virtual future_void close() = 0;

private:
    bool closed{false};
};

};  // namespace asco::io

#endif
