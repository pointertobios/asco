// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/io/file.h>
#include <asco/panic.h>
#include <asco/test/test.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>

namespace {

std::atomic_size_t path_counter{0};

std::filesystem::path unique_path(std::string_view name) {
    auto id = path_counter.fetch_add(1, std::memory_order::relaxed);
    auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / std::format("asco_file_test_{}_{}_{}", name, tick, id);
}

class file_cleanup final {
public:
    explicit file_cleanup(std::filesystem::path path)
            : m_path{std::move(path)} {}

    ~file_cleanup() {
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
    }

    const std::filesystem::path &path() const noexcept { return m_path; }

private:
    std::filesystem::path m_path;
};

void write_text(const std::filesystem::path &path, std::string_view text) {
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::string_view view(const asco::io::buffer<> &buf) {
    return {reinterpret_cast<const char *>(buf.data()), buf.cursor()};
}

}  // namespace

ASCO_TEST(file_at_existing_file_reads_from_beginning) {
    file_cleanup cleanup{unique_path("read_existing")};
    write_text(cleanup.path(), "hello");

    auto opened = co_await asco::io::file::at(cleanup.path()).open();
    ASCO_CHECK(opened.has_value(), "file::at should open an existing file for reading");

    auto file = std::move(opened.value());
    auto read = co_await file.read(5);

    ASCO_CHECK(read.has_value(), "read(5) from an existing readable file should succeed");
    ASCO_CHECK(read->cursor() == 5, "read buffer cursor should equal 5, got {}", read->cursor());
    ASCO_CHECK(view(*read) == "hello", "read content should be 'hello', got '{}'", view(*read));

    ASCO_SUCCESS();
}

ASCO_TEST(file_create_with_write_flag_writes_and_can_read_back) {
    file_cleanup cleanup{unique_path("write_read")};

    auto opened = co_await asco::io::file::create(cleanup.path())
                      .enable_flag(asco::io::open_flags::write)
                      .enable_flag(asco::io::open_flags::truncate)
                      .open();
    ASCO_CHECK(opened.has_value(), "file::create with write flag should open a new file");

    auto file = std::move(opened.value());
    asco::io::buffer<> data{"abcdef"};
    auto written = co_await file.write(data);
    ASCO_CHECK(written.has_value(), "write should succeed on a writable file");
    ASCO_CHECK(*written == 6, "write should report 6 bytes, got {}", *written);

    file.seek(0, asco::io::seek_mode::set);
    auto read = co_await file.read(6);
    ASCO_CHECK(read.has_value(), "read after seek(0, set) should succeed");
    ASCO_CHECK(view(*read) == "abcdef", "read back content should match written content");

    ASCO_SUCCESS();
}

ASCO_TEST(file_read_reports_actual_byte_count_on_short_read_and_eof) {
    file_cleanup cleanup{unique_path("short_read")};
    write_text(cleanup.path(), "abc");

    auto opened = co_await asco::io::file::at(cleanup.path()).open();
    ASCO_CHECK(opened.has_value(), "file::at should open short-read fixture");

    auto file = std::move(opened.value());
    auto first = co_await file.read(8);
    ASCO_CHECK(first.has_value(), "read past EOF should still succeed");
    ASCO_CHECK(
        first->size() == 8, "read buffer capacity should keep requested size 8, got {}", first->size());
    ASCO_CHECK(
        first->cursor() == 3, "short read cursor should be actual byte count 3, got {}", first->cursor());
    ASCO_CHECK(view(*first) == "abc", "short read should expose only bytes read");

    auto second = co_await file.read(8);
    ASCO_CHECK(second.has_value(), "read at EOF should succeed with an empty result");
    ASCO_CHECK(second->cursor() == 0, "EOF read cursor should be 0, got {}", second->cursor());

    ASCO_SUCCESS();
}

ASCO_TEST(file_read_zero_bytes_returns_empty_buffer) {
    file_cleanup cleanup{unique_path("read_zero")};
    write_text(cleanup.path(), "abc");

    auto opened = co_await asco::io::file::at(cleanup.path()).open();
    ASCO_CHECK(opened.has_value(), "file::at should open zero-read fixture");

    auto file = std::move(opened.value());
    auto read = co_await file.read(0);
    ASCO_CHECK(read.has_value(), "read(0) should succeed on a valid file");
    ASCO_CHECK(read->cursor() == 0, "read(0) cursor should be 0, got {}", read->cursor());

    auto after = co_await file.read(3);
    ASCO_CHECK(after.has_value(), "read after read(0) should still succeed");
    ASCO_CHECK(view(*after) == "abc", "read(0) should not advance file position");

    ASCO_SUCCESS();
}

ASCO_TEST(file_seek_modes_clamp_before_start_and_read_from_expected_positions) {
    file_cleanup cleanup{unique_path("seek")};
    write_text(cleanup.path(), "abcdef");

    auto opened = co_await asco::io::file::at(cleanup.path()).open();
    ASCO_CHECK(opened.has_value(), "file::at should open seek fixture");

    auto file = std::move(opened.value());

    file.seek(2, asco::io::seek_mode::set);
    auto from_set = co_await file.read(3);
    ASCO_CHECK(from_set.has_value(), "read after seek(2, set) should succeed");
    ASCO_CHECK(view(*from_set) == "cde", "seek(2, set) should position at 'c'");

    file.seek(-10, asco::io::seek_mode::current);
    auto from_clamped_current = co_await file.read(2);
    ASCO_CHECK(from_clamped_current.has_value(), "read after clamped current seek should succeed");
    ASCO_CHECK(
        view(*from_clamped_current) == "ab", "negative current seek beyond start should clamp to beginning");

    file.seek(-2, asco::io::seek_mode::end);
    auto from_end = co_await file.read(2);
    ASCO_CHECK(from_end.has_value(), "read after seek(-2, end) should succeed");
    ASCO_CHECK(view(*from_end) == "ef", "seek(-2, end) should position two bytes before EOF");

    file.seek(-100, asco::io::seek_mode::end);
    auto from_clamped_end = co_await file.read(2);
    ASCO_CHECK(from_clamped_end.has_value(), "read after clamped end seek should succeed");
    ASCO_CHECK(view(*from_clamped_end) == "ab", "end seek before start should clamp to beginning");

    ASCO_SUCCESS();
}

ASCO_TEST(file_open_member_replaces_handle_and_resets_position) {
    file_cleanup first_cleanup{unique_path("reopen_first")};
    file_cleanup second_cleanup{unique_path("reopen_second")};
    write_text(first_cleanup.path(), "abcdef");
    write_text(second_cleanup.path(), "xy");

    auto opened = co_await asco::io::file::at(first_cleanup.path()).open();
    ASCO_CHECK(opened.has_value(), "file::at should open first file");

    auto file = std::move(opened.value());
    file.seek(4, asco::io::seek_mode::set);

    auto reopened = co_await file.open(
        second_cleanup.path(), asco::io::open_flags::read,
        asco::io::create_mode::read | asco::io::create_mode::write | asco::io::create_mode::read_share);
    ASCO_CHECK(reopened.has_value(), "file::open should replace the current handle with the second file");

    auto read = co_await file.read(2);
    ASCO_CHECK(read.has_value(), "read after reopening should succeed");
    ASCO_CHECK(view(*read) == "xy", "successful reopen should reset file position to the beginning");

    ASCO_SUCCESS();
}

ASCO_TEST(file_move_construct_transfers_handle_and_position) {
    file_cleanup cleanup{unique_path("move_construct")};
    write_text(cleanup.path(), "abcdef");

    auto opened = co_await asco::io::file::at(cleanup.path()).open();
    ASCO_CHECK(opened.has_value(), "file::at should open move fixture");

    auto source = std::move(opened.value());
    auto prefix = co_await source.read(2);
    ASCO_CHECK(prefix.has_value(), "prefix read before move should succeed");

    asco::io::file moved{std::move(source)};
    auto read = co_await moved.read(2);
    ASCO_CHECK(read.has_value(), "moved file should remain readable");
    ASCO_CHECK(view(*read) == "cd", "move construction should preserve current file position");

    bool source_panicked = false;
    try {
        source.seek(0, asco::io::seek_mode::set);
    } catch (asco::panicked &) { source_panicked = true; }
    ASCO_CHECK(source_panicked, "moved-from file should reject operations that require a valid handle");

    ASCO_SUCCESS();
}

ASCO_TEST(file_move_assignment_closes_old_handle_and_transfers_new_handle) {
    file_cleanup first_cleanup{unique_path("move_assign_first")};
    file_cleanup second_cleanup{unique_path("move_assign_second")};
    write_text(first_cleanup.path(), "first");
    write_text(second_cleanup.path(), "second");

    auto first_opened = co_await asco::io::file::at(first_cleanup.path()).open();
    auto second_opened = co_await asco::io::file::at(second_cleanup.path()).open();
    ASCO_CHECK(first_opened.has_value(), "file::at should open first move-assignment fixture");
    ASCO_CHECK(second_opened.has_value(), "file::at should open second move-assignment fixture");

    auto target = std::move(first_opened.value());
    auto source = std::move(second_opened.value());
    auto prefix = co_await source.read(3);
    ASCO_CHECK(prefix.has_value(), "source prefix read before move assignment should succeed");

    target = std::move(source);
    auto read = co_await target.read(3);
    ASCO_CHECK(read.has_value(), "move-assigned file should remain readable");
    ASCO_CHECK(view(*read) == "ond", "move assignment should transfer source handle and position");

    ASCO_SUCCESS();
}

ASCO_TEST(file_open_reports_not_found_for_missing_path) {
    auto missing_dir = unique_path("missing_dir");
    auto missing_path = missing_dir / "missing.txt";

    auto opened = co_await asco::io::file::at(missing_path).open();
    ASCO_CHECK(!opened.has_value(), "opening a missing path should fail");
    ASCO_CHECK(
        opened.error().type == asco::io::open_failed::not_found,
        "missing path should report open_failed::not_found");

    ASCO_SUCCESS();
}

ASCO_TEST(file_create_exclusive_reports_already_exists) {
    file_cleanup cleanup{unique_path("exclusive")};
    write_text(cleanup.path(), "exists");

    auto opened =
        co_await asco::io::file::create(cleanup.path()).enable_flag(asco::io::open_flags::exclusive).open();
    ASCO_CHECK(!opened.has_value(), "exclusive create should fail when the file already exists");
    ASCO_CHECK(
        opened.error().type == asco::io::open_failed::already_exists,
        "exclusive create should report open_failed::already_exists");

    ASCO_SUCCESS();
}

ASCO_TEST(file_read_and_write_report_errors_for_wrong_access_mode) {
    file_cleanup read_only_cleanup{unique_path("read_only")};
    file_cleanup write_only_cleanup{unique_path("write_only")};
    write_text(read_only_cleanup.path(), "abc");

    auto read_only_opened = co_await asco::io::file::at(read_only_cleanup.path()).open();
    ASCO_CHECK(read_only_opened.has_value(), "file::at should open read-only fixture");

    auto read_only = std::move(read_only_opened.value());
    asco::io::buffer<> write_data{"x"};
    auto write = co_await read_only.write(write_data);
    ASCO_CHECK(!write.has_value(), "write should fail on a file opened without write access");

    asco::io::file write_only;
    auto write_open = co_await write_only.open(
        write_only_cleanup.path(),
        asco::io::open_flags::write | asco::io::open_flags::create | asco::io::open_flags::truncate,
        asco::io::create_mode::read | asco::io::create_mode::write | asco::io::create_mode::read_share);
    ASCO_CHECK(write_open.has_value(), "file::open should open write-only fixture");

    auto read = co_await write_only.read(1);
    ASCO_CHECK(!read.has_value(), "read should fail on a file opened without read access");

    ASCO_SUCCESS();
}

ASCO_TEST(file_operations_after_close_or_default_construction_panic) {
    asco::io::file default_file;
    bool default_seek_panicked = false;
    try {
        default_file.seek(0, asco::io::seek_mode::set);
    } catch (asco::panicked &) { default_seek_panicked = true; }
    ASCO_CHECK(default_seek_panicked, "seek on a default-constructed file should panic");

    file_cleanup cleanup{unique_path("closed")};
    write_text(cleanup.path(), "abc");

    auto opened = co_await asco::io::file::at(cleanup.path()).open();
    ASCO_CHECK(opened.has_value(), "file::at should open close fixture");

    auto file = std::move(opened.value());
    file.close();

    bool closed_read_panicked = false;
    try {
        auto read = co_await file.read(1);
        (void)read;
    } catch (asco::panicked &) { closed_read_panicked = true; }
    ASCO_CHECK(closed_read_panicked, "read on a closed file should panic");

    bool closed_write_panicked = false;
    try {
        asco::io::buffer<> data{"x"};
        auto write = co_await file.write(data);
        (void)write;
    } catch (asco::panicked &) { closed_write_panicked = true; }
    ASCO_CHECK(closed_write_panicked, "write on a closed file should panic");

    ASCO_SUCCESS();
}

ASCO_TEST(file_close_is_idempotent) {
    asco::io::file file;

    file.close();
    file.close();

    file_cleanup cleanup{unique_path("close_idempotent")};
    write_text(cleanup.path(), "abc");

    auto opened = co_await asco::io::file::at(cleanup.path()).open();
    ASCO_CHECK(opened.has_value(), "file::at should open close-idempotent fixture");

    file = std::move(opened.value());
    file.close();
    file.close();

    ASCO_SUCCESS();
}
