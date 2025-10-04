// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/future.h>
#include <asco/io/buffer.h>
#include <asco/io/bufio.h>
#include <asco/io/file.h>
#include <asco/io/standard.h>

#include <cassert>
#include <string>
#include <string_view>

using namespace std::literals;
using asco::future;
using asco::io::buffer;

future<int> async_main() {
    auto &so = asco::stdout();
    auto &se = asco::stderr();
    auto &si = asco::stdin();

    // 说明与引导
    co_await so.write(
        buffer<>(std::string{"[standard-io] 本程序用于手动验证异步标准输入输出 (stdin/stdout/stderr)\n"
                             "- 立即输出几行到 stdout 与 stderr；\n"
                             "- 随后读取你在 stdin 输入的多行并回显（Ctrl-D 结束）\n"}));

    // stdout 行缓冲语义演示：先写不含换行，再写带换行的数据，最后 flush
    co_await so.write(buffer<>(std::string_view{"STDOUT: Hello"}));
    co_await so.write(buffer<>(std::string_view{"World\nTail"}));
    // 上面一行写入应在遇到首个 \n 后可见 "HelloWorld\n"，flush 后可见 "Tail"
    co_await so.flush();

    // stderr 直接写（无换行分段缓冲），用于人工观察
    co_await se.write(buffer<>(std::string_view{"STDERR: error-line-1\nerror-line-2"}));

    // 交互读取：提示用户输入若干行，使用 read_lines 逐行读取并回显
    co_await so.write_all(
        buffer<>(std::string{"\n[standard-io] 请输入若干行文本，程序将逐行回显（以 'ECHO:' 前缀）。\n"
                             "输入完成后按 Ctrl-D 结束："}));

    auto gen = si.read_lines();  // 按平台默认换行符
    size_t n = 0;
    while (auto ln = co_await gen()) {
        ++n;
        std::string out = "ECHO:" + *ln + "\n";
        co_await so.write_all(buffer<>(std::move(out)));
    }

    // 演示 read(N) 与 read_all：提示用户再输入一些字节
    co_await so.write_all(
        buffer<>(std::string{"\n[standard-io] 现在请再输入至少 5 个字符并回车（或直接 Ctrl-D 跳过）："}));
    auto part = co_await si.read(5);
    std::string sp = std::move(part).to_string();
    co_await so.write_all(buffer<>(std::string{"READ_PART:" + sp + "\n"}));

    co_await so.write_all(buffer<>(std::string{"[standard-io] 如果还有剩余数据，将读取全部并输出："}));
    auto rest = co_await si.read_all();
    std::string sr = std::move(rest).to_string();
    co_await so.write_all(buffer<>(std::string{"READ_REST:" + sr + "\n"}));

    // 完成
    co_await so.write(buffer<>(std::string{"\n[standard-io] 结束。已读取行数: " + std::to_string(n) + "\n"}));
    co_return 0;
}
