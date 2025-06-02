// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cassert>
#include <iostream>
#include <string>
#include <string_view>

#include <asco/io/buffer.h>

using asco::io::buffer;

int main() {
    buffer buf1;
    buf1.push('A');
    buf1.push('B');
    buf1.push('C');
    assert(buf1.to_string() == "ABC");
    std::cout << "Test 1 passed\n";

    buffer buf2;
    std::string str = "hello";
    buf2.push(std::move(str));
    assert(buf2.to_string() == "hello");
    std::cout << "Test 2 passed\n";

    buffer buf3;
    std::string_view sv = "world";
    buf3.push(sv);
    assert(buf3.to_string() == "world");
    std::cout << "Test 3 passed\n";

    buffer buf4('X');
    assert(buf4.to_string() == "X");
    std::cout << "Test 4 passed\n";

    buffer buf5(std::string("abc"));
    assert(buf5.to_string() == "abc");
    std::cout << "Test 5 passed\n";

    buffer buf6(std::string_view("def"));
    assert(buf6.to_string() == "def");
    std::cout << "Test 6 passed\n";

    buffer buf7;
    buf7.push('1');
    buf7.push(std::string("23"));
    buf7.push(std::string_view("45"));
    assert(buf7.to_string() == "12345");
    std::cout << "Test 7 passed\n";

    buffer buf8;
    buf8.push('a');
    buffer buf9(std::move(buf8));
    assert(buf9.to_string() == "a");
    std::cout << "Test 8 passed\n";

    buffer buf10;
    buf10.push('x');
    buffer buf11;
    buf11.push('y');
    buf10.push(std::move(buf11));
    assert(buf10.to_string() == "xy");
    std::cout << "Test 9 passed\n";

    buffer buf12;
    buf12.push('z');
    assert(buf12.to_string() == "z");
    assert(buf12.to_string().empty());
    std::cout << "Test 10 passed\n";

    std::cout << "All buffer tests passed!" << std::endl;
    return 0;
}
