// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/exception.h>
#include <asco/future.h>
#include <asco/io/buffer.h>
#include <asco/io/file.h>

#include <cassert>
#include <cstring>
#include <print>
#include <string>
#include <string_view>

using asco::future, asco::file, asco::buffer;

future<int> async_main() {
    {
        std::println("--- Normal tests ---");

        {
            std::string testpath = "asco_test_file.txt";

            {
                auto ores = co_await file::at(testpath)
                                .create()
                                .mode(0644)
                                .write()  //
                                .open();
                assert(ores.has_value());
                auto f = std::move(ores.value());

                buffer buf(std::string_view("ASCO file operation test\n"));
                buf.push(std::string("test string1\n"));
                buf.push(std::string("test string2\n"));

                auto res = co_await f.write(std::move(buf));
                if (res)
                    std::println("rest buffer: {}", std::move(*res).to_string());
                assert(!res);

                std::println("Test file write passed.");
            }

            {
                auto ores = co_await file::at(testpath).read().open();
                assert(ores.has_value());
                auto f = std::move(ores.value());

                f.seekg(10);
                auto partial = co_await f.read(10);
                assert(partial.size() == 10);
                std::println("First 10 bytes: \'{}\'.", std::move(partial).to_string());
            }
            ::system(std::format("rm {}", testpath).c_str());
        }

        {
            auto ores1 = co_await file::at("exclusive.txt")
                             .create()
                             .mode(0644)
                             .exclusive()
                             .write()  //
                             .open();
            assert(ores1.has_value());

            auto ores2 = co_await file::at("exclusive.txt")
                             .create()
                             .mode(0644)
                             .exclusive()
                             .write()  //
                             .open();
            assert(!ores2.has_value());

            auto ores3 = co_await file::at("truncate.txt")
                             .create()
                             .mode(0644)
                             .truncate()
                             .write()  //
                             .open();
            assert(ores1.has_value());

            std::println("Open mode test passed.");
        }
        ::system("rm exclusive.txt truncate.txt");
    }

    {
        std::string tmppath = "empty.txt";
        file f;
        {
            auto ores = co_await file::at(tmppath)
                            .create()
                            .mode(0644)
                            .read()  //
                            .open();
            assert(ores.has_value());
            f = std::move(ores.value());
        }

        auto content = co_await f.read(10);
        assert(content.size() == 0);

        co_await f.close();

        ::system(std::format("rm {}", tmppath).c_str());

        try {
            auto _ = co_await f.read(10);
            assert(false);
        } catch (const std::exception &e) { std::println("Close test passed"); }
    }

    {
        std::println("\n--- Testing append mode ---");
        std::string appendpath = "append_test.txt";

        {
            auto ores = co_await file::at(appendpath)
                            .create()
                            .mode(0644)
                            .write()  //
                            .open();
            assert(ores.has_value());
            auto f = std::move(ores.value());

            buffer buf(std::string_view("Initial content\n"));
            auto res = co_await f.write(std::move(buf));
            assert(!res);
        }

        {
            auto ores = co_await file::at(appendpath)
                            .write()
                            .append()  //
                            .open();
            assert(ores.has_value());
            auto f = std::move(ores.value());

            buffer buf(std::string_view("Appended content\n"));
            auto res = co_await f.write(std::move(buf));
            assert(!res);
        }

        {
            auto ores = co_await file::at(appendpath)
                            .read()  //
                            .open();
            assert(ores.has_value());
            auto f = std::move(ores.value());

            auto content = co_await f.read(1024);
            std::string content_str = std::move(content).to_string();
            std::println("Append test content: '{}'", content_str);
            assert(content_str.find("Initial content") != std::string::npos);
            assert(content_str.find("Appended content") != std::string::npos);
        }

        ::system(std::format("rm {}", appendpath).c_str());
        std::println("Append mode test passed.");
    }

    {
        std::println("\n--- Testing seek operations ---");
        std::string seekpath = "seek_test.txt";

        {
            auto ores = co_await file::at(seekpath)
                            .create()
                            .mode(0644)
                            .write()  //
                            .open();
            assert(ores.has_value());
            auto f = std::move(ores.value());

            buffer buf(std::string_view("ABCDEFGHIJKLMNOPQRSTUVWXYZ"));
            auto res = co_await f.write(std::move(buf));
            assert(!res);
        }

        {
            auto ores = co_await file::at(seekpath)
                            .read()
                            .write()  //
                            .open();
            assert(ores.has_value());
            auto f = std::move(ores.value());

            f.seekg(5, file::seekpos::begin);
            assert(f.tellg() == 5);
            auto content1 = co_await f.read(5);
            std::println("Read from position 5: '{}'", std::move(content1).to_string());

            f.seekg(-3, file::seekpos::current);
            assert(f.tellg() == 7);
            auto content2 = co_await f.read(3);
            std::println("Read after seekg(-3): '{}'", std::move(content2).to_string());

            f.seekg(-5, file::seekpos::end);
            assert(f.tellg() == 21);
            auto content3 = co_await f.read(5);
            std::println("Read 5 chars from end(-5): '{}'", std::move(content3).to_string());

            f.seekp(10, file::seekpos::begin);
            assert(f.tellp() == 10);
            buffer wbuf(std::string_view("**MODIFIED**"));
            auto res = co_await f.write(std::move(wbuf));
            assert(!res);

            f.seekg(0, file::seekpos::begin);
            auto final_content = co_await f.read(100);
            std::println("Final content after seek/write: '{}'", std::move(final_content).to_string());
        }

        ::system(std::format("rm {}", seekpath).c_str());
        std::println("Seek operations test passed.");
    }

    {
        std::println("\n--- Testing error handling ---");

        {
            auto ores = co_await file::at("/nonexistent/file.txt")
                            .read()  //
                            .open();
            assert(!ores.has_value());
            std::println("Open nonexistent file error: {}", std::strerror(-ores.error()));
        }

        if (::access("/etc/shadow", F_OK) == 0) {
            auto ores = co_await file::at("/etc/shadow")
                            .write()  //
                            .open();
            assert(!ores.has_value());
            std::println("Open no-permission file error: {}", std::strerror(-ores.error()));
        }

        std::println("Error handling test passed.");
    }

    co_return 0;
}
