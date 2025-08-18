// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/future.h>
#include <asco/io/buffer.h>
#include <asco/io/bufio.h>
#include <asco/io/file.h>

#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>

using asco::block_read_writer;
using asco::future;
using asco::io::buffer;
using asco::io::file;
using asco::io::seekpos;
using namespace std::literals;

static future<std::string> read_all(block_read_writer<file> &brw) {
    std::string acc;
    brw.seekg(0, seekpos::begin);
    while (true) {
        auto ob = co_await brw.read(4096);
        if (!ob)
            break;
        auto s = std::move(*ob).to_string();
        if (s.empty())
            break;
        acc += std::move(s);
    }
    co_return acc;
}

future<int> async_main() {
    const auto path = "blockrw.txt"sv;

    auto r = co_await file::at(path).read().write().create().truncate().mode(0644).open();
    assert(r.has_value());
    block_read_writer<file> brw(std::move(*r));

    {
        co_await brw.write(buffer("Hello"sv));
        assert(brw.tellp() == 5);
        co_await brw.write(buffer("World"sv));
        assert(brw.tellp() == 10);

        auto content = co_await read_all(brw);
        assert(content == "HelloWorld");
        std::cout << "blockrw Test 1 passed\n";
    }

    {
        co_await brw.flush();

        auto content = co_await read_all(brw);
        assert(content == "HelloWorld");

        std::cout << "blockrw Test 2 passed\n";
    }

    {
        brw.seekp(20, seekpos::begin);
        co_await brw.write(buffer("X"sv));
        assert(brw.tellp() == 21);

        auto s = co_await read_all(brw);
        assert(s.size() == 21);
        assert(s.substr(0, 10) == "HelloWorld");
        for (size_t i = 10; i < 20; ++i) assert(s[i] == '\0');
        assert(s[20] == 'X');

        co_await brw.flush();
        std::cout << "blockrw Test 3 passed\n";
    }

    {
        brw.seekp(6, seekpos::begin);          // "HelloW" | "orld"
        co_await brw.write(buffer("ASCO"sv));  // -> "HelloWASCO..."
        assert(brw.tellp() == 10);
        auto s = co_await read_all(brw);
        assert(s.substr(0, 6) == "HelloW");
        assert(s.substr(6, 4) == "ASCO");
        for (size_t i = 10; i < 20; ++i) assert(s[i] == '\0');
        assert(s[20] == 'X');

        std::cout << "blockrw Test 4 passed\n";
    }

    {
        brw.seekg(0, seekpos::begin);

        auto r1 = co_await brw.read(3);
        assert(r1 && std::move(*r1).to_string() == "Hel");

        auto r2 = co_await brw.read(5);
        assert(r2 && std::move(*r2).to_string() == "loWAS");

        auto r3 = co_await brw.read(20);
        assert(r3);
        auto s3 = std::move(*r3).to_string();
        assert(s3.size() == 13);
        assert(s3.substr(0, 2) == "CO");
        for (size_t i = 2; i < 11; ++i) assert(s3[i] == '\0');
        assert(s3[12] == 'X');

        auto r4 = co_await brw.read(1);
        assert(!r4.has_value());  // EOF
        assert(brw.tellg() == 21);

        std::cout << "blockrw Test 5 passed\n";
    }

    {
        brw.seekp(0, seekpos::begin);
        co_await brw.write(buffer("AB"sv));
        assert(brw.tellp() == 2);

        brw.seekg(0, seekpos::begin);
        auto r1 = co_await brw.read(2);
        assert(r1 && std::move(*r1).to_string() == "AB");

        std::cout << "blockrw Test 6 passed\n";
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);

    co_return 0;
}
