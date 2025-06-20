// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_IO_BUFFER_H
#define ASCO_IO_BUFFER_H 1

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>

#include <asco/exception.h>
#include <asco/utils/pubusing.h>

namespace asco::io {

using namespace types;

template<simple_char CharT = char>
class buffer {
public:
    explicit buffer() {}

    buffer(const buffer &rhs) = delete;
    buffer(buffer &&rhs)
            : buffer_chain(std::move(rhs.buffer_chain))
            , size_sum(rhs.size_sum) {}

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

    size_t size() const { return size_sum; }

    bool empty() const { return size_sum == 0; }

    void push(CharT value) {
        if (buffer_chain.empty() || buffer_chain.back()->frame->ends) {
            auto frame = std::make_shared<buffer_frame>(
                ({
                    auto *p = new CharT[origin_size];
                    p[0] = value;
                    p;
                }),
                origin_size, 1);
            buffer_chain.push_back(std::make_unique<shared_frame>(frame, 0, 1));
        } else {
            auto &node = buffer_chain.back();
            node->size += 1;
            node->frame->push(value);
        }
        size_sum += 1;
    }

    void push(std::basic_string<CharT> &&str) {
        if (str.size() <= sso_size && !buffer_chain.empty()) {
            size_t first_section = 0;
            if (!buffer_chain.empty() && !buffer_chain.back()->frame->ends) {
                first_section = std::min(str.size(), buffer_chain.back()->frame->rest());

                auto &node = buffer_chain.back();
                node->size += first_section;
                node->frame->push(str.data(), first_section);
            }

            size_t rest_size = str.size() - first_section;
            if (rest_size > 0) {
                auto frame = std::make_shared<buffer_frame>(
                    ({
                        auto *p = new CharT[origin_size];  // origin_size is always greater than sso_size
                        std::memcpy(p, str.data() + first_section, rest_size);
                        p;
                    }),
                    origin_size, rest_size);
                buffer_chain.push_back(std::make_unique<shared_frame>(frame, 0, rest_size));
            }

            return;
        }

        if (!buffer_chain.empty())
            buffer_chain.back()->frame->ends = true;
        auto size = str.size();
        auto frame = std::make_shared<buffer_frame>(std::move(str), size, size, true);
        buffer_chain.push_back(std::make_unique<shared_frame>(frame, 0, size));
        size_sum += size;
    }

    void push(const std::basic_string_view<CharT> &str) {
        if (str.size() <= sso_size && !buffer_chain.empty()) {
            size_t first_section = 0;
            if (!buffer_chain.empty() && !buffer_chain.back()->frame->ends) {
                first_section = std::min(str.size(), buffer_chain.back()->frame->rest());

                auto &node = buffer_chain.back();
                node->size += first_section;
                node->frame->push(str.data(), first_section);
            }

            size_t rest_size = str.size() - first_section;
            if (rest_size > 0) {
                auto frame = std::make_shared<buffer_frame>(
                    ({
                        auto *p = new CharT[origin_size];  // origin_size is always greater than sso_size
                        std::memcpy(p, str.data() + first_section, rest_size);
                        p;
                    }),
                    origin_size, rest_size);
                buffer_chain.push_back(std::make_unique<shared_frame>(frame, 0, rest_size));
            }

            return;
        }

        if (!buffer_chain.empty())
            buffer_chain.back()->frame->ends = true;
        auto size = str.size();
        auto frame = std::make_shared<buffer_frame>(str, size, size, true);
        buffer_chain.push_back(std::make_unique<shared_frame>(frame, 0, size));
        size_sum += size;
    }

    void push(buffer &&buf) {
        if (!buffer_chain.empty())
            buffer_chain.back()->frame->ends = true;
        buffer_chain.insert(
            buffer_chain.end(), std::make_move_iterator(buf.buffer_chain.begin()),
            std::make_move_iterator(buf.buffer_chain.end()));
        size_sum += buf.size_sum;

        buf.size_sum = 0;
        buf.buffer_chain.clear();
    }

    void swap(buffer &rhs) {
        buffer_chain.swap(rhs.buffer_chain);
        std::swap(size_sum, rhs.size_sum);
    }

    void clear() {
        buffer_chain.clear();
        size_sum = 0;
    }

    std::tuple<buffer, buffer> split(this buffer &&self, size_t pos) {
        buffer first, second;

        size_t current_size{0};
        for (auto it = std::make_move_iterator(self.buffer_chain.begin());
             it != std::make_move_iterator(self.buffer_chain.end()); it++) {
            auto frame_size = (*it)->size;
            auto new_size = current_size + frame_size;
            if (current_size < pos && new_size <= pos) {
                first.buffer_chain.push_back(*it);
                first.size_sum += frame_size;
            } else if (current_size >= pos) {
                second.buffer_chain.push_back(*it);
                second.size_sum += frame_size;
            } else {
                auto [a, b] = shared_frame::split(*it, pos - current_size);
                first.buffer_chain.push_back(std::move(a));
                first.size_sum += pos - current_size;
                second.buffer_chain.push_back(std::move(b));
                second.size_sum += frame_size + current_size - pos;
            }
            current_size = new_size;
        }

        if (current_size < pos)
            throw asco::exception("buffer::split(): pos out of range.");

        self.buffer_chain.clear();
        self.size_sum = 0;

        return std::make_tuple(std::move(first), std::move(second));
    }

    std::basic_string<CharT> to_string(this buffer &&self) {
        std::basic_string<CharT> res;
        for (auto &frame : self.buffer_chain) {
            auto start = frame->start;
            auto size = frame->size;
            std::visit(
                [&](auto &buf) {
                    if constexpr (std::is_same_v<std::remove_reference_t<decltype(buf)>, CharT *>)
                        res.append(buf + start, buf + start + size);
                    else if constexpr (std::is_same_v<
                                           std::remove_reference_t<decltype(buf)>, std::basic_string<CharT>>)
                        res.append(buf, start, size);
                    else if constexpr (std::is_same_v<
                                           std::remove_reference_t<decltype(buf)>,
                                           std::basic_string_view<CharT>>)
                        res.append(buf, start, size);
                },
                frame->frame->buffer);
        }
        self.buffer_chain.clear();
        self.size_sum = 0;
        return res;
    }

private:
    // The buffer `buf` must be allocated by operator new[]
    void push_raw_array_buffer(CharT *buf, size_t capacity, size_t size) {
        if (!buffer_chain.empty())
            buffer_chain.back()->ends = true;
        auto frame = std::make_shared<buffer_frame>(buf, capacity, size, true);
        buffer_chain.push_back(std::make_unique<shared_frame>(frame, 0, size));
        size_sum += size;
    }

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

        size_t rest() {
            if (ends)
                return 0;
            else
                return capacity - size;
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
                            "[ASCO] asco::io::buffer::buffer_frame::push(CharT): This frame is not a raw array.");
                    }
                },
                buffer);
        }

        void push(const CharT *str, size_t size) {
            if (size > rest())
                throw asco::inner_exception(
                    "[ASCO] asco::io::buffer::buffer_frame::push(CharT *, size_t): This frame has not any more space for pushing.");

            std::visit(
                [&](auto &buf) {
                    if constexpr (std::is_same_v<std::remove_reference_t<decltype(buf)>, CharT *>) {
                        std::memcpy(buf + this->size, str, size);
                        this->size += size;
                        if (this->size == capacity)
                            ends = true;
                    } else {
                        throw asco::inner_exception(
                            "[ASCO] asco::io::buffer::buffer_frame::push(CharT *, size_t): This frame is not a raw array.");
                    }
                },
                buffer);
        }
    };

    struct shared_frame {
        std::shared_ptr<buffer_frame> frame;
        size_t start;
        size_t size;

        static auto split(std::unique_ptr<shared_frame> self, size_t offset) {
            return std::make_tuple(
                std::make_unique<shared_frame>(self->frame, self->start, offset),
                std::make_unique<shared_frame>(self->frame, self->start + offset, self->size - offset));
        }

        auto view() const { return std::basic_string_view<CharT>(frame->buffer + start, size); }
    };

    std::vector<std::unique_ptr<shared_frame>> buffer_chain;

    size_t size_sum{0};
};

};  // namespace asco::io

#endif
