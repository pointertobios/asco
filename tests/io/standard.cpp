// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/future.h>
#include <asco/io/buffer.h>
#include <asco/io/standard.h>
#include <asco/print.h>

#include <cassert>
#include <string>

using namespace std::literals;
using asco::future;

future<int> async_main() {
    auto &so = asco::stdout();
    auto &si = asco::stdin();

    co_await asco::print(
        "[standard-io] 本程序用于手动验证异步标准输入输出 (stdin/stdout/stderr)\n"
        "- 立即输出几行到 stdout 与 stderr；\n"
        "- 随后读取你在 stdin 输入的多行并回显（Ctrl-D 结束）\n");

    co_await asco::print("STDOUT: Hello");
    co_await asco::print("World\nTail");
    co_await so.flush();

    co_await asco::eprint("STDERR: error-line-1\nerror-line-2");

    co_await asco::print(
        "\n[standard-io] 请输入若干行文本，程序将逐行回显（以 'ECHO:' 前缀）。\n"
        "输入完成后按 Ctrl-D 结束：");

    auto gen = si.read_lines();
    size_t n = 0;
    while (auto ln = co_await gen()) {
        ++n;
        co_await asco::println("ECHO:{}", *ln);
    }

    co_await asco::print("\n[standard-io] 现在请再输入至少 5 个字符并回车（或直接 Ctrl-D 跳过）：");
    auto part = co_await si.read(5);
    std::string sp = std::move(part).to_string();
    co_await asco::println("READ_PART:{}", sp);

    co_await asco::print("[standard-io] 如果还有剩余数据，将读取全部并输出：");
    auto rest = co_await si.read_all();
    std::string sr = std::move(rest).to_string();
    co_await asco::println("READ_REST:{}", sr);

    co_await asco::println("\n[standard-io] 结束。已读取行数: {}", n);
    co_return 0;
}
