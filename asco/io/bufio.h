// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_IO_STREAM_H
#define ASCO_IO_STREAM_H 1

#include <asco/future.h>
#include <asco/io/buffer.h>
#include <asco/utils/concepts.h>
#include <asco/utils/math.h>
#include <asco/utils/pubusing.h>

#include <limits>
#include <optional>

namespace asco::io {

using namespace types;
using namespace concepts;

enum class seekpos { begin, current, end };

enum class read_result {
    eof,
    interrupted,
    again,
};

using std::same_as, std::declval;

template<typename T>
concept block_read = requires(T t) {
    { t.read(size_t{}) } -> same_as<future<std::expected<buffer<>, read_result>>>;
    { t.tellg() } -> std::same_as<size_t>;
    { t.seekg(ssize_t{}, seekpos{}) } -> same_as<size_t>;
} && move_secure<T>;

template<typename T>
concept block_write = requires(T t) {
    { t.write(buffer<>{}) } -> same_as<future<std::optional<buffer<>>>>;
    { t.tellp() } -> same_as<size_t>;
    { t.seekp(ssize_t{}, seekpos{}) } -> same_as<size_t>;
} && move_secure<T>;

template<typename T>
concept block_read_write = requires(T t) {
    typename T::file_raw_handle;
    typename T::file_state;
    { T::raw_handle_invalid } -> same_as<const typename T::file_raw_handle &>;
    { t.raw_handle() } -> same_as<typename T::file_raw_handle>;
    { t.state() } -> same_as<typename T::file_state>;
    { t._seek_fp(declval<size_t &>(), ssize_t{}, declval<seekpos>(), size_t{}) } -> same_as<size_t>;
} && requires(typename T::file_state st) {
    { st.size() } -> same_as<size_t>;
} && block_read<T> && block_write<T>;

template<typename T>
concept stream_read = requires(T t) {
    { t.read(size_t{}) } -> same_as<future<std::expected<buffer<>, read_result>>>;
} && !block_read<T> && move_secure<T>;

template<typename T>
concept stream_write = requires(T t) {
    { t.write(buffer<>{}) } -> same_as<future<std::optional<buffer<>>>>;
} && !block_write<T> && move_secure<T>;

template<typename T>
concept stream_read_write = stream_read<T> && stream_write<T>;

template<block_read_write T>
class block_read_writer {
public:
    block_read_writer(T &&ioo)
            : ioo{std::move(ioo)} {}

    block_read_writer(const T &ioo)
            : ioo{std::move(ioo)} {}

    ~block_read_writer() {
        is_destructor_flush = true;
        flush();
        while (!destructor_can_exit.test());
    }

    // Abortable
    future<std::optional<buffer<>>> read(size_t nbytes) {
        struct re {
            ~re() {
                if (this_coro::aborted())
                    this_coro::throw_coroutine_abort<future<std::optional<buffer<>>>>();
            }
        } restorer;

        auto load_size = math::min_exp2_from(nbytes);
        auto load_start = ({
            size_t res;
            if (pread)
                res = math::min_exp2_from(pread);
            else
                res = 0;
            res;
        });

        if (active_buffer.empty()) {
            while (true) {
                ioo.seekg(load_start, seekpos::begin);

                if (this_coro::aborted())
                    throw coroutine_abort{};

                auto b = co_await ioo.read(load_size);

                if (!b.has_value()) {
                    if (auto e = b.error(); e == read_result::eof)
                        co_return std::nullopt;
                } else {
                    active_buffer.push(std::move(*b));
                    buffer_start = load_start;
                    break;
                }
            }

            if (this_coro::aborted())
                throw coroutine_abort{};
        } else if (load_start < buffer_start) {
            while (true) {
                ioo.seekg(load_start, seekpos::begin);

                if (this_coro::aborted())
                    throw coroutine_abort{};

                // There is no need to consider about this case:
                // End of the required section is after the active_buffer.
                // Because we are not ask to read all the required data even when the data is accessible now.
                auto b = co_await ioo.read(buffer_start - load_start);

                if (this_coro::aborted())
                    throw coroutine_abort{};

                if (!b.has_value()) {
                    if (auto e = b.error(); e == read_result::eof)
                        co_return std::nullopt;
                } else {
                    b.value().push(std::move(active_buffer));
                    active_buffer.push(std::move(*b));
                    buffer_start = load_start;
                    break;
                }
            }
        } else if (auto buffer_end = buffer_start + active_buffer.size(); load_start >= buffer_end) {
            while (true) {
                ioo.seekg(buffer_end, seekpos::begin);

                if (this_coro::aborted())
                    throw coroutine_abort{};

                auto b = co_await ioo.read(load_start + load_size - buffer_end);

                if (this_coro::aborted())
                    throw coroutine_abort{};

                if (!b.has_value()) {
                    if (auto e = b.error(); e == read_result::eof)
                        co_return std::nullopt;
                } else {
                    active_buffer.push(std::move(*b));
                    break;
                }
            }
        }

        buffer<> res{};
        if (auto buf = std::get<1>(active_buffer.clone().split(pread - buffer_start)); buf.size()) {
            if (buf.size() > nbytes)
                res = std::get<0>(buf.clone().split(nbytes));
            else
                res = std::move(buf);
        }

        pread += res.size();

        if (this_coro::aborted())
            throw coroutine_abort{};

        co_return std::move(res);
    }

    // Unabortable
    future<void> write(buffer<> buf) {
        auto inbuf_size = buf.size();
        if (auto acbuf_size = active_buffer.size(), buffer_end = buffer_start + acbuf_size;
            pwrite > buffer_end) {
            auto buffer_fill_ = read_ioo_until_eof(buffer_end, pwrite - buffer_end);
            active_buffer.fill_zero(pwrite - buffer_end);
            active_buffer.push(std::move(buf));
            if (auto buffer_fill = co_await buffer_fill_)
                active_buffer.modify(acbuf_size, std::move(*buffer_fill));
        } else if (auto inbuf_end = pwrite + buf.size(); buffer_start > inbuf_end) {
            auto buffer_fill_ = read_ioo_until_eof(inbuf_end, buffer_start - inbuf_end);
            buf.fill_zero(buffer_start - inbuf_end);
            buf.push(std::move(active_buffer));
            if (auto buffer_fill = co_await buffer_fill_)
                buf.modify(inbuf_size, std::move(*buffer_fill));
            active_buffer = std::move(buf);
            buffer_start = pwrite;
        } else if (buffer_start > pwrite) {
            buf.modify(buffer_start - pwrite, std::move(active_buffer));
            active_buffer = std::move(buf);
            buffer_start = pwrite;
        } else {
            active_buffer.modify(pwrite - buffer_start, std::move(buf));
        }
        pwrite += inbuf_size;
        co_return;
    }

    __asco_always_inline size_t tellg() const noexcept { return pread; }

    __asco_always_inline size_t tellp() const noexcept { return pwrite; }

    __asco_always_inline size_t seekg(ssize_t offset, seekpos whence = seekpos::current) {
        return ioo._seek_fp(
            pread, offset, whence, std::max(ioo.state().size(), buffer_start + active_buffer.size()));
    }

    // Write to position after EOF is allowed
    __asco_always_inline size_t seekp(ssize_t offset, seekpos whence = seekpos::current) {
        return ioo._seek_fp(pwrite, offset, whence, std::numeric_limits<size_t>::max());
    }

    // Unabortable
    future<void> flush() {
        auto bstart = buffer_start;
        buffer_start = 0;
        auto acbuf = std::move(active_buffer);
        if (is_destructor_flush)
            destructor_can_exit.test_and_set();

        if (!acbuf.empty()) {
            ioo.seekp(bstart, seekpos::begin);
            co_await ioo.write(std::move(acbuf));
        }
        co_return;
    }

private:
    T ioo;  // IO object

    size_t buffer_start{0};
    buffer<> active_buffer;
    size_t pread{0};
    size_t pwrite{0};

    bool is_destructor_flush{false};
    // flush() use this flag to inform the destructor that it can continue.
    atomic_flag destructor_can_exit;

    // Abortable
    future<std::optional<buffer<>>> read_ioo_until_eof(size_t start, size_t nbytes) {
        struct re {
            ~re() {
                if (this_coro::aborted())
                    this_coro::throw_coroutine_abort<future<std::optional<buffer<>>>>();
            }
        } restorer;

        if (this_coro::aborted())
            throw coroutine_abort{};

        if (auto now = ioo.seekg(start, seekpos::begin); now < start)
            co_return std::nullopt;

        while (true) {
            auto res = co_await ioo.read(nbytes);

            if (this_coro::aborted())
                throw coroutine_abort{};

            if (!res.has_value()) {
                if (auto e = res.error(); e == read_result::eof)
                    co_return std::nullopt;
                else if (e == read_result::again)
                    co_await std::suspend_always{};
            } else {
                co_return std::move(*res);
            }
        }
    }
};

template<stream_read T>
class stream_reader {};

template<stream_write T>
class stream_writer {};

template<stream_read_write T>
class stream_read_writer {};

};  // namespace asco::io

namespace asco {

using io::block_read, io::block_write, io::block_read_write;
using io::block_read_writer;
using io::seekpos;
using io::stream_read, io::stream_write, io::stream_read_write;
using io::stream_reader, io::stream_writer, io::stream_read_writer;

};  // namespace asco

#endif
