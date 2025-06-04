// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_IO_BUFFER_H
#define ASCO_IO_BUFFER_H 1

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include <asco/exception.h>
#include <asco/utils/pubusing.h>

namespace asco::io {

using namespace types;

template<typename CharT = char>
    requires std::is_integral_v<CharT> || std::is_same_v<CharT, char> || std::is_same_v<CharT, std::byte>
class buffer {
public:
    explicit buffer() {
        buffer_chain.push_back(std::make_unique<buffer_frame>(new CharT[origin_size], origin_size, 0));
    }

    buffer(const buffer &rhs) = delete;
    buffer(buffer &&rhs)
            : buffer_chain(std::move(rhs.buffer_chain)) {}

    buffer(CharT value)
            : buffer() {
        push(value);
    }

    buffer(std::basic_string<CharT> &&str)
            : buffer() {
        push(std::move(str));
    }

    buffer(const std::basic_string_view<CharT> &str)
            : buffer() {
        push(str);
    }

    void push(CharT value) {
        if (buffer_chain.empty() || buffer_chain.back()->ends) {
            buffer_chain.push_back(
                std::make_unique<buffer_frame>(
                    ({
                        auto *p = new CharT[origin_size];
                        p[0] = value;
                        p;
                    }),
                    origin_size, 1));
        } else {
            buffer_chain.back()->push(value);
            if (buffer_chain.back()->size == buffer_chain.back()->capacity)
                buffer_chain.back()->ends = true;
        }
    }

    void push(std::basic_string<CharT> &&str) {
        if (!buffer_chain.empty())
            buffer_chain.back()->ends = true;
        auto size = str.size();
        buffer_chain.push_back(std::make_unique<buffer_frame>(std::move(str), size, size, true));
    }

    void push(const std::basic_string_view<CharT> &str) {
        if (!buffer_chain.empty())
            buffer_chain.back()->ends = true;
        buffer_chain.push_back(std::make_unique<buffer_frame>(str, str.size(), str.size(), true));
    }

    void push(buffer &&buf) {
        if (!buffer_chain.empty())
            buffer_chain.back()->ends = true;
        buffer_chain.insert(
            buffer_chain.end(), std::make_move_iterator(buf.buffer_chain.begin()),
            std::make_move_iterator(buf.buffer_chain.end()));
        buf.buffer_chain.clear();
    }

    void swap(buffer &rhs) { buffer_chain.swap(rhs.buffer_chain); }

    std::basic_string<CharT> to_string() {
        std::basic_string<CharT> res;
        for (auto &frame : buffer_chain) {
            std::visit(
                [&](auto &buf) {
                    if constexpr (std::is_same_v<std::remove_reference_t<decltype(buf)>, CharT *>)
                        res.append(buf, buf + frame->size);
                    else if constexpr (std::is_same_v<
                                           std::remove_reference_t<decltype(buf)>, std::basic_string<CharT>>)
                        res.append(buf);
                    else
                        res.append_range(buf);
                },
                frame->buffer);
        }
        buffer_chain.clear();
        return res;
    }

private:
    constexpr inline static size_t origin_size = 64;
    constexpr inline static size_t sso_size = 16;

    struct buffer_frame {
        std::variant<CharT *, std::basic_string<CharT>, std::basic_string_view<CharT>> buffer;
        size_t capacity;
        size_t size;
        bool ends{false};

        ~buffer_frame() {
            std::visit(
                [](auto &buf) {
                    if constexpr (std::is_same_v<std::remove_reference_t<decltype(buf)>, CharT *>)
                        delete[] buf;
                },
                buffer);
        }

        void push(CharT value) {
            std::visit(
                [&](auto &buf) {
                    if constexpr (std::is_same_v<std::remove_reference_t<decltype(buf)>, CharT *>) {
                        buf[size++] = value;
                        if (size == capacity)
                            ends = true;
                    } else {
                        throw asco::inner_exception(
                            "[ASCO] asco::io::buffer::buffer_frame::push(T) inner: This frame is not a raw array.");
                    }
                },
                buffer);
        }
    };
    std::vector<std::unique_ptr<buffer_frame>> buffer_chain;
};

};  // namespace asco::io

#endif
