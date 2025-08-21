// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#define BUFIO_IMPL
#include <asco/io/bufio.h>
#include <asco/io/file.h>

namespace asco::io {

template class block_read_writer<file>;
template class stream_reader<file>;
template class stream_writer<file>;

};  // namespace asco::io
