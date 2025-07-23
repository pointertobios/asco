// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include "asco/rterror.h"
#include <cassert>
#include <iostream>
#include <string>
#include <string_view>

#include <asco/exception.h>
#include <asco/future.h>
#include <asco/io/buffer.h>

using asco::future;
using asco::io::buffer;

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

    buffer buf5(std::string("abc"));
    assert(std::move(buf5).to_string() == "abc");
    std::cout << "Test 5 passed\n";

    buffer buf6(std::string_view("def"));
    assert(std::move(buf6).to_string() == "def");
    std::cout << "Test 6 passed\n";

    buffer buf7;
    buf7.push('1');
    buf7.push(std::string("23"));
    buf7.push(std::string_view("45"));
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

    // swap 测试
    buffer buf13;
    buf13.push('a');
    buffer buf14;
    buf14.push('b');
    buf13.swap(buf14);
    assert(std::move(buf13).to_string() == "b");
    assert(std::move(buf14).to_string() == "a");
    std::cout << "Test 11 (swap) passed\n";

    // split 测试
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

    // split 边界与异常测试
    buffer buf16;
    buf16.push('x');
    buf16.push('y');
    try {
        auto [l, r] = std::move(buf16).split(5);
        assert(false);  // 不应到达此处
    } catch (const asco::runtime_error &) { std::cout << "Test 13 (split out of range) passed\n"; }

    // size() 测试
    buffer buf17;
    buf17.push('a');
    buf17.push('b');
    assert(buf17.size() == 2);
    buf17.push('c');
    assert(buf17.size() == 3);
    std::move(buf17).to_string();
    assert(buf17.size() == 0);
    std::cout << "Test 14 (size) passed\n";

    std::cout << "All buffer tests passed!" << std::endl;
    co_return 0;
}
