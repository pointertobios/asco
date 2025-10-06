// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/exception.h>
#include <asco/future.h>
#include <asco/io/buffer.h>
#include <asco/io/file.h>

#include <asco/print.h>
#include <cassert>
#include <string>
#include <string_view>

using asco::future, asco::file, asco::buffer, asco::seekpos;

future<int> async_main() {
    {
        asco::println("--- Normal tests ---");

        {
            {
                auto ores = co_await file::at("asco_test_file.txt")
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
                    asco::println("rest buffer: {}", std::move(*res).to_string());
                assert(!res);

                asco::println("Test file write passed.");
            }

            {
                auto ores = co_await file::at("asco_test_file.txt").read().open();
                assert(ores.has_value());
                auto f = std::move(ores.value());

                f.seekg(10);
                auto partial = (co_await f.read(10)).value_or(buffer{});
                assert(partial.size() == 10);
                asco::println("First 10 bytes: '{}' .", std::move(partial).to_string());
            }
            ::system(std::format("rm {}", "asco_test_file.txt").c_str());
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

            asco::println("Open mode test passed.");
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

        auto content = (co_await f.read(10)).value_or(buffer{});
        assert(content.size() == 0);

        co_await f.close();

        ::system(std::format("rm {}", tmppath).c_str());

        bool close_ok = false;
        try {
            auto _ = co_await f.read(10);
            assert(false);
        } catch (const std::exception &e) { close_ok = true; }
        if (close_ok)
            asco::println("Close test passed");
    }

    {
        asco::println("\n--- Testing append mode ---");
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

            auto content = (co_await f.read(1024)).value_or(buffer{});
            std::string content_str = std::move(content).to_string();
            asco::println("Append test content: '{}'", content_str);
            assert(content_str.find("Initial content") != std::string::npos);
            assert(content_str.find("Appended content") != std::string::npos);
        }

        ::system(std::format("rm {}", appendpath).c_str());
        asco::println("Append mode test passed.");
    }

    {
        asco::println("\n--- Testing seek operations ---");
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

            f.seekg(5, seekpos::begin);
            assert(f.tellg() == 5);
            auto content1 = (co_await f.read(5)).value_or(buffer{});
            asco::println("Read from position 5: '{}'", std::move(content1).to_string());

            f.seekg(-3, seekpos::current);
            assert(f.tellg() == 7);
            auto content2 = (co_await f.read(3)).value_or(buffer{});
            asco::println("Read after seekg(-3): '{}'", std::move(content2).to_string());

            f.seekg(-5, seekpos::end);
            assert(f.tellg() == 21);
            auto content3 = (co_await f.read(5)).value_or(buffer{});
            asco::println("Read 5 chars from end(-5): '{}'", std::move(content3).to_string());

            f.seekp(10, seekpos::begin);
            assert(f.tellp() == 10);
            buffer wbuf(std::string_view("**MODIFIED**"));
            auto res = co_await f.write(std::move(wbuf));
            assert(!res);

            f.seekg(0, seekpos::begin);
            auto final_content = (co_await f.read(100)).value_or(buffer{});
            asco::println("Final content after seek/write: '{}'", std::move(final_content).to_string());
        }

        ::system(std::format("rm {}", seekpath).c_str());
        asco::println("Seek operations test passed.");
    }

    {
        asco::println("\n--- Testing error handling ---");

        {
            auto ores = co_await file::at("/nonexistent/file.txt")
                            .read()  //
                            .open();
            assert(!ores.has_value());
        }

        if (::access("/etc/shadow", F_OK) == 0) {
            auto ores = co_await file::at("/etc/shadow")
                            .write()  //
                            .open();
            assert(!ores.has_value());
        }

        asco::println("Error handling test passed.");
    }

    co_return 0;
}
