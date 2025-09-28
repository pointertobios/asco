// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/future.h>
#include <asco/generator.h>
#include <asco/io/buffer.h>
#include <asco/io/bufio.h>
#include <asco/io/file.h>

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace std::literals;
using asco::future;
using asco::generator;
using asco::io::buffer;
using asco::io::file;
using asco::io::newline;
using asco::io::stream_reader;
using asco::io::stream_writer;

static future<void> write_file(std::string_view path, const std::string &content) {
    auto r = co_await file::at(path).read().write().create().truncate().mode(0644).open();
    assert(r.has_value());
    file f = std::move(*r);
    co_await f.write(buffer<>(std::move(content)));
    co_await f.close();
    co_return;
}

static future<file> open_read_only(std::string_view path) {
    auto r = co_await file::at(path).read().open();
    assert(r.has_value());
    co_return std::move(*r);
}

future<int> async_main() {
    {
        const auto path = "streamrw_empty.txt"sv;
        co_await write_file(path, "");

        auto f = co_await open_read_only(path);
        stream_reader<file> sr(std::move(f));

        auto all = co_await sr.read_all();
        auto s = std::move(all).to_string();
        assert(s.empty());

        auto lines = sr.read_lines();  // default LF
        size_t cnt = 0;
        while (auto ln = co_await lines()) {
            (void)ln;
            ++cnt;
        }
        assert(cnt == 0);

        std::filesystem::remove(path);
        std::cout << "streamrw Test 1 passed\n";
    }

    {
        const auto path = "streamrw_lf.txt"sv;
        std::vector<std::string> lines_vec = {"Hello", "World", "", "ASCO"};
        std::string content;
        for (size_t i = 0; i < lines_vec.size(); ++i) {
            content += lines_vec[i];
            if (i + 1 != lines_vec.size())
                content += '\n';
        }
        co_await write_file(path, content);

        {
            auto f = co_await open_read_only(path);
            stream_reader<file> sr(std::move(f));
            auto all = co_await sr.read_all();
            assert(std::move(all).to_string() == content);
        }

        auto f_lines = co_await open_read_only(path);
        stream_reader<file> sr_lines(std::move(f_lines));
        auto gl = sr_lines.read_lines(newline::lf);
        std::vector<std::string> got;
        while (auto s = co_await gl()) { got.push_back(*s); }
        assert(got == lines_vec);

        std::filesystem::remove(path);
        std::cout << "streamrw Test 2 passed\n";
    }

    {
        const auto path = "streamrw_cr.txt"sv;
        std::string content = "A\rB\rC";  // Expect lines: A, B, C
        co_await write_file(path, content);

        auto f = co_await open_read_only(path);
        stream_reader<file> sr(std::move(f));

        auto gl = sr.read_lines(newline::cr);
        std::vector<std::string> got;
        while (auto s = co_await gl()) { got.push_back(*s); }
        assert((got == std::vector<std::string>{"A", "B", "C"}));

        std::filesystem::remove(path);
        std::cout << "streamrw Test 3 passed\n";
    }

    {
        const auto path = "streamrw_crlf_boundary.txt"sv;
        // Construct 4095 'A' then '\r' (total 4096) so next chunk starts with '\n'
        std::string line1(4095, 'A');
        std::string content = line1;
        content += '\r';  // position 4095
        content += '\n';  // position 4096, starts next chunk
        content += "Line2\r\n";
        content += "TailNoNL";  // no final newline

        co_await write_file(path, content);

        auto f = co_await open_read_only(path);
        stream_reader<file> sr(std::move(f));

        auto gl = sr.read_lines(newline::crlf);
        std::vector<std::string> got;
        while (auto s = co_await gl()) { got.push_back(*s); }
        assert(got.size() == 3);
        assert(got[0] == line1);
        assert(got[1] == "Line2");
        assert(got[2] == "TailNoNL");

        // Also validate read_all
        auto f2 = co_await open_read_only(path);
        stream_reader<file> sr2(std::move(f2));
        auto all = co_await sr2.read_all();
        assert(std::move(all).to_string() == content);

        std::filesystem::remove(path);
        std::cout << "streamrw Test 4 passed\n";
    }

    {
        const auto path = "streamrw_read_seq.txt"sv;
        std::string content(10000, '\0');
        for (size_t i = 0; i < content.size(); ++i) content[i] = static_cast<char>('A' + (i % 26));
        co_await write_file(path, content);

        auto f = co_await open_read_only(path);
        stream_reader<file> sr(std::move(f));

        auto b1 = co_await sr.read(4096);
        assert(b1.size() == 4096);
        auto s1 = std::move(b1).to_string();
        assert(s1 == content.substr(0, 4096));

        auto b2 = co_await sr.read(4096);
        assert(b2.size() == 4096);
        auto s2 = std::move(b2).to_string();
        assert(s2 == content.substr(4096, 4096));

        auto b3 = co_await sr.read(4096);  // only 1808 remain
        assert(b3.size() == 10000 - 8192);
        auto s3 = std::move(b3).to_string();
        assert(s3 == content.substr(8192));

        auto b4 = co_await sr.read(1);  // EOF now
        assert(b4.size() == 0);

        std::filesystem::remove(path);
        std::cout << "streamrw Test 5 passed\n";
    }

    {
        const auto path = "streamrw_read_exact.txt"sv;
        std::string content(4096, 'Z');
        co_await write_file(path, content);

        auto f = co_await open_read_only(path);
        stream_reader<file> sr(std::move(f));
        auto b = co_await sr.read(4096);
        assert(b.size() == 4096);
        assert(std::move(b).to_string() == content);
        auto eofb = co_await sr.read(10);
        assert(eofb.size() == 0);
        std::filesystem::remove(path);
        std::cout << "streamrw Test 6 passed\n";
    }

    {
        const auto path = "streamrw_read_small.txt"sv;
        std::string content = "ShortData";  // 9 bytes
        co_await write_file(path, content);
        auto f = co_await open_read_only(path);
        stream_reader<file> sr(std::move(f));
        auto b = co_await sr.read(1024);
        assert(b.size() == content.size());
        assert(std::move(b).to_string() == content);
        auto eofb = co_await sr.read(1);
        assert(eofb.size() == 0);
        std::filesystem::remove(path);
        std::cout << "streamrw Test 7 passed\n";
    }

    {
        const auto path = "streamrw_read_empty.txt"sv;
        co_await write_file(path, "");
        auto f = co_await open_read_only(path);
        stream_reader<file> sr(std::move(f));
        auto b = co_await sr.read(512);
        assert(b.size() == 0);
        auto b2 = co_await sr.read(1);
        assert(b2.size() == 0);
        std::filesystem::remove(path);
        std::cout << "streamrw Test 8 passed\n";
    }

    // --- stream_writer tests ---
    {
        const auto path = "streamrw_writer_basic.txt"sv;
        auto r = co_await file::at(path).read().write().create().truncate().mode(0644).open();
        assert(r.has_value());
        {
            stream_writer<file> sw(std::move(*r));
            co_await sw.write(buffer<>(std::string_view{"Hello"}));
            co_await sw.flush();
            {
                auto rf = co_await open_read_only(path);
                stream_reader<file> sr(std::move(rf));
                auto all = co_await sr.read_all();
                assert(std::move(all).to_string() == "Hello");
            }

            co_await sw.write(buffer<>(std::string_view{"World\nTail"}));
            {
                auto rf = co_await open_read_only(path);
                stream_reader<file> sr(std::move(rf));
                auto all = co_await sr.read_all();
                assert(std::move(all).to_string() == "HelloWorld\n");
            }
            // Flush 后应追加 Tail
            co_await sw.flush();
            {
                auto rf = co_await open_read_only(path);
                stream_reader<file> sr(std::move(rf));
                auto all = co_await sr.read_all();
                assert(std::move(all).to_string() == "HelloWorld\nTail");
            }
        }
        std::filesystem::remove(path);
        std::cout << "streamrw Test 9 (writer basic) passed\n";
    }

    struct test_partial_state {
        size_t expected{0};
        size_t written{0};
        std::string collected;
        int step{0};
    };

    struct partial_mock {
        test_partial_state *st;

        future<std::optional<buffer<>>> write(buffer<> buf) {
            if (buf.empty())
                co_return std::nullopt;
            if (st->step == 0 && buf.size() > 1) {
                // 第一次写：只写前半，返回后半 remainder，模拟部分写
                size_t half = buf.size() / 2;
                auto [left, right] = std::move(buf).split(half);
                st->written += left.size();
                st->collected += std::move(left).to_string();
                st->step = 1;
                co_return std::optional<buffer<>>{std::move(right)};
            } else {
                st->written += buf.size();
                st->collected += std::move(buf).to_string();
                co_return std::nullopt;
            }
        }
    };

    {
        test_partial_state st;
        st.expected = 12;
        partial_mock pm{&st};
        {
            stream_writer<partial_mock> sw(std::move(pm));
            co_await sw.write(buffer<>(std::string_view{"HelloWorld!!"}));
            co_await sw.flush();
        }
        assert(st.written == st.expected);
        std::cout << "streamrw Test 10 (writer partial flush) passed\n";
    }

    {
        test_partial_state st;
        st.expected = 10;
        {
            partial_mock pm{&st};
            stream_writer<partial_mock> sw(std::move(pm));
            co_await sw.write(buffer<>(std::string_view{"ABCDEFGHIJ"}));
        }
        assert(st.written == st.expected);
        std::cout << "streamrw Test 11 (writer dtor partial flush) passed\n";
    }

    {
        const auto path = "streamrw_srw_basic.txt"sv;
        auto r = co_await file::at(path).read().write().create().truncate().mode(0644).open();
        assert(r.has_value());
        {
            asco::io::stream_read_writer<file> srw(std::move(*r));

            co_await srw.write(buffer<>(std::string_view{"Hello"}));
            {
                auto rf = co_await open_read_only(path);
                stream_reader<file> sr(std::move(rf));
                auto all = co_await sr.read_all();
                assert(std::move(all).to_string().empty());
            }

            co_await srw.write(buffer<>(std::string_view{"World\nTail"}));
            {
                auto rf = co_await open_read_only(path);
                stream_reader<file> sr(std::move(rf));
                auto all = co_await sr.read_all();
                assert(std::move(all).to_string() == "HelloWorld\n");
            }

            co_await srw.flush();
            {
                auto rf = co_await open_read_only(path);
                stream_reader<file> sr(std::move(rf));
                auto all = co_await sr.read_all();
                assert(std::move(all).to_string() == "HelloWorld\nTail");
            }
        }
        std::filesystem::remove(path);
        std::cout << "streamrw Test 12 (stream_read_writer basic) passed\n";
    }

    {
        const auto path = "streamrw_srw_dtor_flush.txt"sv;
        auto r = co_await file::at(path).read().write().create().truncate().mode(0644).open();
        assert(r.has_value());
        {
            asco::io::stream_read_writer<file> srw(std::move(*r));
            co_await srw.write(buffer<>(std::string_view{"NoFlushData"}));
        }
        {
            auto rf = co_await open_read_only(path);
            stream_reader<file> sr(std::move(rf));
            auto all = co_await sr.read_all();
            assert(std::move(all).to_string() == "NoFlushData");
        }
        std::filesystem::remove(path);
        std::cout << "streamrw Test 13 (stream_read_writer dtor flush) passed\n";
    }

    std::cout << "All streamrw tests passed.\n";
    co_return 0;
}
