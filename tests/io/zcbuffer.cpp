// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <cassert>
#include <iostream>
#include <string>
#include <string_view>

#include <asco/exception.h>
#include <asco/future.h>
#include <asco/io/buffer.h>

using asco::future;
using asco::io::buffer;
using namespace std::literals;

future<int> async_main() {
    buffer buf1;
    buf1.push('A');
    buf1.push('B');
    buf1.push('C');
    assert(std::move(buf1).to_string() == "ABC");
    std::cout << "Test 1 passed\n";

    buffer buf2;
    std::string str = "hello";
    buf2.push(std::move(str));
    assert(std::move(buf2).to_string() == "hello");
    std::cout << "Test 2 passed\n";

    buffer buf3;
    std::string_view sv = "world";
    buf3.push(sv);
    assert(std::move(buf3).to_string() == "world");
    std::cout << "Test 3 passed\n";

    buffer buf4('X');
    assert(std::move(buf4).to_string() == "X");
    std::cout << "Test 4 passed\n";

    buffer buf5("abc"s);
    assert(std::move(buf5).to_string() == "abc");
    std::cout << "Test 5 passed\n";

    buffer buf6("def"sv);
    assert(std::move(buf6).to_string() == "def");
    std::cout << "Test 6 passed\n";

    buffer buf7;
    buf7.push('1');
    buf7.push("23"s);
    buf7.push("45"sv);
    assert(std::move(buf7).to_string() == "12345");
    std::cout << "Test 7 passed\n";

    buffer buf8;
    buf8.push('a');
    buffer buf9(std::move(buf8));
    assert(std::move(buf9).to_string() == "a");
    std::cout << "Test 8 passed\n";

    buffer buf10;
    buf10.push('x');
    buffer buf11;
    buf11.push('y');
    buf10.push(std::move(buf11));
    assert(std::move(buf10).to_string() == "xy");
    std::cout << "Test 9 passed\n";

    buffer buf12;
    buf12.push('z');
    assert(std::move(buf12).to_string() == "z");
    assert(std::move(buf12).to_string().empty());
    std::cout << "Test 10 passed\n";

    {
        buffer buf13;
        buf13.push('a');
        buffer buf14;
        buf14.push('b');
        buf13.swap(buf14);
        assert(std::move(buf13).to_string() == "b");
        assert(std::move(buf14).to_string() == "a");
        std::cout << "Test 11 (swap) passed\n";
    }

    {
        buffer buf15;
        buf15.push('1');
        buf15.push('2');
        buf15.push('3');
        buf15.push('4');
        buf15.push('5');
        auto [left, right] = std::move(buf15).split(2);
        assert(left.size() == 2);
        assert(right.size() == 3);
        assert(std::move(left).to_string() == "12");
        assert(std::move(right).to_string() == "345");
        std::cout << "Test 12 (split) passed\n";
    }

    {
        buffer buf16;
        buf16.push('x');
        buf16.push('y');
        try {
            auto [l, r] = std::move(buf16).split(5);
            assert(false);
        } catch (const asco::runtime_error &) { std::cout << "Test 13 (split out of range) passed\n"; }
    }

    {
        buffer buf17;
        buf17.push('a');
        buf17.push('b');
        assert(buf17.size() == 2);
        buf17.push('c');
        assert(buf17.size() == 3);
        std::move(buf17).to_string();
        assert(buf17.size() == 0);
        std::cout << "Test 14 (size) passed\n";
    }

    {
        buffer bufA;
        bufA.push('A');
        bufA.push("SCO"s);
        auto cloned = bufA.clone();
        assert(bufA.size() == 4);
        assert(cloned.size() == 4);
        auto s1 = std::move(cloned).to_string();
        assert(s1 == "ASCO");
        auto cloned2 = bufA.clone();
        auto s2 = std::move(cloned2).to_string();
        assert(s2 == "ASCO");
        std::cout << "Test 15 (clone basic) passed\n";
    }

    {
        buffer bufA;
        bufA.push("Hello"sv);
        auto c1 = bufA.clone();
        auto s1 = std::move(c1).to_string();
        assert(s1 == "Hello");
        auto c2 = bufA.clone();
        auto s2 = std::move(c2).to_string();
        assert(s2 == "Hello");
        std::cout << "Test 16 (clone independence) passed\n";
    }

    {
        buffer bufA;
        bufA.push("HelloWorld"sv);
        buffer insert;
        insert.push("ASCO"sv);
        bufA.modify(5, std::move(insert));
        auto s = std::move(bufA).to_string();
        assert(s == "HelloASCOd");
        std::cout << "Test 17 (modify mid shorter) passed\n";
    }

    {
        buffer bufA;
        bufA.push("HelloWorld"sv);
        buffer insert;
        insert.push("ASCOOOL"sv);
        bufA.modify(5, std::move(insert));
        auto s = std::move(bufA).to_string();
        assert(s == "HelloASCOOOL");
        std::cout << "Test 18 (modify mid longer) passed\n";
    }

    {
        buffer bufA;
        bufA.push("Hello"sv);
        buffer repl;
        repl.push("AS"sv);
        bufA.modify(0, std::move(repl));
        auto s = std::move(bufA).to_string();
        assert(s == "ASllo");
        std::cout << "Test 19 (modify prefix) passed\n";
    }

    {
        buffer bufA;
        bufA.push("Data"sv);
        buffer add;
        add.push(""sv);
        bufA.modify(4, std::move(add));
        auto s = std::move(bufA).to_string();
        assert(s == "Data");
        std::cout << "Test 20 (modify append) passed\n";
    }

    {
        buffer bufA;
        bufA.push("ABCDEFG"sv);
        buffer tail;
        tail.push("XYZ"sv);
        bufA.modify(4, std::move(tail));
        auto s = std::move(bufA).to_string();
        assert(s == "ABCDXYZ");
        std::cout << "Test 21 (modify tail exact) passed\n";
    }

    {
        buffer bufA;
        bufA.push("OldContent"sv);
        buffer repl;
        repl.push("New"sv);
        bufA.modify(0, std::move(repl));
        auto s = std::move(bufA).to_string();
        assert(s == "NewContent");
        std::cout << "Test 22 (modify full replace) passed\n";
    }

    {
        buffer buf;
        assert(buf.empty());
        buf.push('X');
        assert(!buf.empty());
        assert(buf.buffer_count() >= 1);
        buf.clear();
        assert(buf.empty());
        assert(buf.size() == 0);
        assert(buf.buffer_count() == 0);
        std::cout << "Test 23 (empty/clear/buffer_count) passed\n";
    }

    {
        buffer buf;
        buf.push('X');
        buf.fill_zero(5);
        auto s = std::move(buf).to_string();
        std::string expect;
        expect.push_back('X');
        expect.append(5, '\0');
        assert(s == expect);
        std::cout << "Test 24 (fill_zero small) passed\n";
    }

    {
        buffer buf;
        buf.push('A');
        buf.fill_zero(5000);  // > origin_size(4096)
        buf.push('B');
        auto s = std::move(buf).to_string();
        std::string expect;
        expect.push_back('A');
        expect.append(5000, '\0');
        expect.push_back('B');
        assert(s == expect);
        std::cout << "Test 25 (fill_zero large) passed\n";
    }

    {
        buffer buf;
        buf.push('A');
        std::string big(100, 'q');  // > sso_size(32)
        buf.push(std::move(big));
        assert(buf.buffer_count() >= 2);
        auto s = std::move(buf).to_string();
        assert(s == std::string("A") + std::string(100, 'q'));
        std::cout << "Test 26 (push string large) passed\n";
    }

    {
        buffer buf;
        buf.push("EDGE"sv);
        {
            auto [l, r] = std::move(buf).clone().split(0);
            assert(l.size() == 0);
            assert(std::move(r).to_string() == "EDGE");
        }
        {
            auto copy = buf.clone();
            auto [l, r] = std::move(copy).split(copy.size());
            assert(std::move(l).to_string() == "EDGE");
            assert(r.size() == 0);
        }
        std::cout << "Test 27 (split boundaries) passed\n";
    }

    {
        buffer src;
        src.push("MOVE"sv);
        buffer dst;
        dst = std::move(src);
        assert(src.size() == 0);
        assert(std::move(dst).to_string() == "MOVE");
        std::cout << "Test 28 (move assignment) passed\n";
    }

    {
        buffer buf;
        buf.push("ABC"sv);
        buffer repl;
        repl.push("X"sv);
        bool thrown = false;
        try {
            buf.modify(10, std::move(repl));
        } catch (const asco::runtime_error &) { thrown = true; }
        assert(thrown);
        std::cout << "Test 29 (modify out of range) passed\n";
    }

    {
        buffer buf;
        auto *p = new char[8];
        p[0] = 'A';
        p[1] = 'A';
        p[2] = '\0';
        buf.push_raw_array_buffer(p, 8, 2);
        std::string s(40, 'b');
        buf.push(std::move(s));
        std::string backing(100, 'c');
        std::string_view sv(backing);
        buf.push(sv);

        std::string joined = std::move(buf).to_string();
        assert(joined == "AA"s + std::string(40, 'b') + std::string(100, 'c'));
        std::cout << "Test 30 (rawbuffers iterate) passed\n";
    }

    {
        static int destroy_count = 0;
        auto deleter = [](char *buf) noexcept {
            destroy_count++;
            delete[] buf;
        };
        {
            buffer buf;
            auto *p = new char[4];
            p[0] = 'R';
            p[1] = 'A';
            p[2] = 'W';
            p[3] = '\0';
            buf.push_raw_array_buffer(p, 4, 3, deleter);
            auto s = std::move(buf).to_string();
            assert(s == "RAW");
            buf.clear();
        }
        assert(destroy_count == 1);
        std::cout << "Test 31 (push_raw_array_buffer deleter) passed\n";
    }

    {
        buffer buf;
        buf.push("ZZZ"sv);
        (void)std::move(buf).to_string();
        assert(buf.buffer_count() == 0);
        std::cout << "Test 32 (to_string clears frames) passed\n";
    }

    std::cout << "All buffer tests passed!" << std::endl;
    co_return 0;
}
