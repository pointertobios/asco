// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_IO_BUFFER_H
#define ASCO_IO_BUFFER_H 1

#include <asco/rterror.h>
#include <asco/utils/concepts.h>
#include <asco/utils/pubusing.h>

#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>

namespace asco::io {

using namespace types;
using namespace concepts;

template<simple_char CharT = char>
class buffer {
public:
    using char_type = CharT;

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

    buffer &operator=(buffer &&rhs) {
        buffer_chain = std::move(rhs.buffer_chain);
        size_sum = rhs.size_sum;
        rhs.size_sum = 0;
        return *this;
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

            size_sum += str.size();

            return;
        }

        if (!buffer_chain.empty())
            buffer_chain.back()->frame->ends = true;
        auto size = str.size();
        auto frame = std::make_shared<buffer_frame>(std::move(str), size, size, nullptr, true);
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

            size_sum += str.size();

            return;
        }

        if (!buffer_chain.empty())
            buffer_chain.back()->frame->ends = true;
        auto size = str.size();
        auto frame = std::make_shared<buffer_frame>(str, size, size, nullptr, true);
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
                auto [a, b] = std::move(**it).split(pos - current_size);
                first.buffer_chain.push_back(std::move(a));
                first.size_sum += pos - current_size;
                second.buffer_chain.push_back(std::move(b));
                second.size_sum += frame_size + current_size - pos;
            }
            current_size = new_size;
        }

        if (current_size < pos)
            throw runtime_error("buffer::split(): pos out of range.");

        self.buffer_chain.clear();
        self.size_sum = 0;

        return std::make_tuple(std::move(first), std::move(second));
    }

    std::basic_string<CharT> to_string(this buffer &&self) {
        std::basic_string<CharT> res;
        for (auto &frame : self.buffer_chain) {
            std::visit(
                [&res, start = frame->start, size = frame->size](auto &buf) {
                    if constexpr (std::is_same_v<std::remove_reference_t<decltype(buf)>, CharT *>)
                        res.append(buf + start, buf + start + size);
                    else
                        res.append(buf, start, size);
                },
                frame->frame->buffer);
        }
        self.buffer_chain.clear();
        self.size_sum = 0;
        return res;
    }

    size_t buffer_count() const noexcept { return buffer_chain.size(); }

private:
    constexpr inline static size_t origin_size = 256;
    constexpr inline static size_t sso_size = 32;

    struct buffer_frame {
        using buffer_destroyer = void(CharT *) noexcept;

        std::variant<CharT *, std::basic_string<CharT>, std::basic_string_view<CharT>> buffer;
        size_t capacity;
        size_t size;

        // Available while buffer is of type CharT *.
        // On deconstruction, call this function to free the raw array bufer if not nullptr.
        // Or use delete[] by default.
        buffer_destroyer *rawarr_de{nullptr};

        bool ends{false};

        ~buffer_frame() {
            std::visit(
                [rawarr_de = this->rawarr_de](auto &buf) {
                    if constexpr (std::is_same_v<std::remove_reference_t<decltype(buf)>, CharT *>) {
                        if (!rawarr_de) {
                            delete[] buf;
                        } else {
                            rawarr_de(buf);
                        }
                    }
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
                        throw runtime_error(
                            "[ASCO] asco::io::buffer::buffer_frame::push(CharT): This frame is not a raw array.");
                    }
                },
                buffer);
        }

        void push(const CharT *str, size_t size) {
            if (size > rest())
                throw runtime_error(
                    "[ASCO] asco::io::buffer::buffer_frame::push(CharT *, size_t): This frame has not any more space for pushing.");

            std::visit(
                [&](auto &buf) {
                    if constexpr (std::is_same_v<std::remove_reference_t<decltype(buf)>, CharT *>) {
                        std::memcpy(buf + this->size, str, size);
                        this->size += size;
                        if (this->size == capacity)
                            ends = true;
                    } else {
                        throw runtime_error(
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

        auto split(this shared_frame &&self, size_t offset) {
            return std::make_tuple(
                std::make_unique<shared_frame>(self.frame, self.start, offset),
                std::make_unique<shared_frame>(self.frame, self.start + offset, self.size - offset));
        }

        auto view() const { return std::basic_string_view<CharT>(frame->buffer + start, size); }
    };

    std::vector<std::unique_ptr<shared_frame>> buffer_chain;

    size_t size_sum{0};

public:
    // If rawarr_de is nullptr, the buffer `buf` must be allocated by operator new[], so that buffer_frame
    // will free it with delete[].
    void push_raw_array_buffer(
        CharT *buf, size_t capacity, size_t size, buffer_frame::buffer_destroyer *rawarr_de = nullptr) {
        if (!buffer_chain.empty())
            buffer_chain.back()->frame->ends = true;
        auto frame = std::make_shared<buffer_frame>(buf, capacity, size, rawarr_de, true);
        buffer_chain.push_back(std::make_unique<shared_frame>(frame, 0, size));
        size_sum += size;
    }
    struct rawbuffer_iterator {
        struct rawbuffer {
            void *buffer;
            size_t size;

            size_t seq;
        };

        rawbuffer_iterator(
            std::vector<std::unique_ptr<shared_frame>>::const_iterator it,
            std::vector<std::unique_ptr<shared_frame>>::const_iterator end) noexcept
                : it(it)
                , end(end) {}

        rawbuffer operator*() const {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-stack-address"
            uint8_t *ptr = static_cast<uint8_t *>(std::visit(
                [](auto buf) {
                    if constexpr (std::is_same_v<std::remove_reference_t<decltype(buf)>, CharT *>)
                        return static_cast<void *>(buf);
                    else
                        return const_cast<void *>(reinterpret_cast<const void *>(buf.data()));
                },
                (*it)->frame->buffer));
#pragma clang diagnostic pop

            void *buf = ptr + (*it)->start;
            return {buf, (*it)->size, seq};
        }

        const rawbuffer_iterator &operator++() const {
            it++;
            seq++;
            return *this;
        }

        const rawbuffer_iterator &operator++(int) const { return ++*this; }

        bool is_end() const { return it == end; }

    private:
        size_t mutable seq{0};
        std::vector<std::unique_ptr<shared_frame>>::const_iterator mutable it;
        std::vector<std::unique_ptr<shared_frame>>::const_iterator end;
    };

    rawbuffer_iterator rawbuffers() const noexcept { return {buffer_chain.begin(), buffer_chain.end()}; }
};

};  // namespace asco::io

namespace asco {

template<concepts::simple_char CharT = char>
using buffer = io::buffer<CharT>;

};

#endif
