// Copyright (C) 2026 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/io/buffer.h>
#include <asco/test/test.h>

#include <cstddef>
#include <cstring>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>

namespace {

template<typename Alloc>
std::string_view view(const asco::io::buffer<Alloc> &buf) {
    return {reinterpret_cast<const char *>(buf.data()), buf.cursor()};
}

class tracking_resource final : public std::pmr::memory_resource {
public:
    std::size_t allocations{0};
    std::size_t deallocations{0};
    std::size_t allocated_bytes{0};
    std::size_t deallocated_bytes{0};

private:
    void *do_allocate(std::size_t bytes, std::size_t alignment) override {
        ++allocations;
        allocated_bytes += bytes;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void *p, std::size_t bytes, std::size_t alignment) override {
        ++deallocations;
        deallocated_bytes += bytes;
        std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
    }

    bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override {
        return this == &other;
    }
};

using tracked_allocator = std::pmr::polymorphic_allocator<std::byte>;
using tracked_buffer = asco::io::buffer<tracked_allocator>;

}  // namespace

ASCO_TEST(buffer_default_constructs_empty_and_can_grow_on_first_push) {
    asco::io::buffer<> buf;

    ASCO_CHECK(buf.size() == 0, "default buffer size should be 0, got {}", buf.size());
    ASCO_CHECK(buf.cursor() == 0, "default buffer cursor should be 0, got {}", buf.cursor());
    ASCO_CHECK(buf.data() == nullptr, "default buffer data should be null");

    auto pushed = buf.push("hello");
    ASCO_CHECK(pushed == 5, "first push should write 5 bytes, got {}", pushed);
    ASCO_CHECK(buf.size() == 5, "first push should allocate size 5, got {}", buf.size());
    ASCO_CHECK(buf.cursor() == 5, "first push should move cursor to 5, got {}", buf.cursor());
    ASCO_CHECK(view(buf) == "hello", "buffer content mismatch after first push");

    auto second = buf.push("!");
    ASCO_CHECK(second == 0, "push into full buffer should write 0 bytes, got {}", second);
    ASCO_CHECK(buf.cursor() == 5, "failed push should not change cursor, got {}", buf.cursor());
    ASCO_CHECK(view(buf) == "hello", "failed push should not change content");

    ASCO_SUCCESS();
}

ASCO_TEST(buffer_fixed_capacity_push_truncates_to_remaining_space) {
    asco::io::buffer<> buf{8};

    auto first = buf.push("abc");
    ASCO_CHECK(first == 3, "first push should write 3 bytes, got {}", first);
    ASCO_CHECK(buf.size() == 8, "fixed buffer size should stay 8, got {}", buf.size());
    ASCO_CHECK(buf.cursor() == 3, "cursor should be 3 after first push, got {}", buf.cursor());

    auto second = buf.push("defghijk");
    ASCO_CHECK(second == 5, "second push should write only remaining 5 bytes, got {}", second);
    ASCO_CHECK(buf.cursor() == 8, "cursor should reach capacity 8, got {}", buf.cursor());
    ASCO_CHECK(view(buf) == "abcdefgh", "buffer should contain truncated input");

    ASCO_SUCCESS();
}

ASCO_TEST(buffer_push_span_and_push_buffer_use_source_cursor) {
    std::byte bytes[] = {
        static_cast<std::byte>('a'),
        static_cast<std::byte>('b'),
        static_cast<std::byte>('c'),
        static_cast<std::byte>('d'),
    };
    asco::io::buffer<> span_target{6};

    auto span_pushed = span_target.push(std::span<const std::byte>{bytes, 4});
    ASCO_CHECK(span_pushed == 4, "span push should write 4 bytes, got {}", span_pushed);
    ASCO_CHECK(view(span_target) == "abcd", "span push content mismatch");

    asco::io::buffer<> source{8};
    ASCO_CHECK(source.push("xy") == 2, "source push should write 2 bytes");

    asco::io::buffer<> target{6};
    ASCO_CHECK(target.push("12") == 2, "target prefix push should write 2 bytes");
    auto copied = target.push(source);
    ASCO_CHECK(copied == 2, "push(buffer) should copy source cursor bytes only, got {}", copied);
    ASCO_CHECK(target.cursor() == 4, "target cursor should be 4, got {}", target.cursor());
    ASCO_CHECK(view(target) == "12xy", "push(buffer) content mismatch");

    ASCO_SUCCESS();
}

ASCO_TEST(buffer_string_constructor_copies_input) {
    std::string text = "stable";
    asco::io::buffer<> buf{std::string_view{text}};

    text[0] = 'u';

    ASCO_CHECK(buf.size() == 6, "string buffer size should be 6, got {}", buf.size());
    ASCO_CHECK(buf.cursor() == 6, "string buffer cursor should be 6, got {}", buf.cursor());
    ASCO_CHECK(view(buf) == "stable", "string constructor should copy input content");

    ASCO_SUCCESS();
}

ASCO_TEST(buffer_allocator_only_constructor_starts_empty_and_uses_allocator_on_first_push) {
    tracking_resource resource;
    tracked_allocator allocator{&resource};
    tracked_buffer buf{allocator};

    ASCO_CHECK(buf.size() == 0, "allocator-only buffer size should be 0, got {}", buf.size());
    ASCO_CHECK(buf.cursor() == 0, "allocator-only buffer cursor should be 0, got {}", buf.cursor());
    ASCO_CHECK(buf.data() == nullptr, "allocator-only buffer data should be null");
    ASCO_CHECK(resource.allocations == 0, "allocator-only construction should not allocate immediately");

    auto pushed = buf.push("alloc");
    ASCO_CHECK(pushed == 5, "first push should write 5 bytes, got {}", pushed);
    ASCO_CHECK(view(buf) == "alloc", "allocator-only buffer content mismatch");
    ASCO_CHECK(resource.allocations == 1, "first push should allocate through provided allocator");
    ASCO_CHECK(
        resource.allocated_bytes == 5, "first push should allocate 5 bytes, got {}",
        resource.allocated_bytes);

    buf.clear();
    ASCO_CHECK(resource.deallocations == 1, "clear should deallocate through provided allocator");
    ASCO_CHECK(
        resource.deallocated_bytes == resource.allocated_bytes, "allocator deallocated byte count mismatch");

    ASCO_SUCCESS();
}

ASCO_TEST(buffer_rvalue_allocator_only_constructor_owns_allocator) {
    tracking_resource resource;

    {
        tracked_buffer buf{tracked_allocator{&resource}};
        ASCO_CHECK(buf.size() == 0, "rvalue allocator-only buffer size should be 0, got {}", buf.size());
        ASCO_CHECK(
            buf.cursor() == 0, "rvalue allocator-only buffer cursor should be 0, got {}", buf.cursor());
        ASCO_CHECK(buf.data() == nullptr, "rvalue allocator-only buffer data should be null");

        auto pushed = buf.push("tmp");
        ASCO_CHECK(pushed == 3, "first push should write 3 bytes, got {}", pushed);
        ASCO_CHECK(view(buf) == "tmp", "rvalue allocator-only buffer content mismatch");
    }

    ASCO_CHECK(resource.allocations == 1, "rvalue allocator-only buffer should allocate once");
    ASCO_CHECK(resource.deallocations == 1, "rvalue allocator-only buffer should deallocate on destruction");
    ASCO_CHECK(resource.allocated_bytes == 3, "rvalue allocator-only buffer should allocate 3 bytes");
    ASCO_CHECK(resource.deallocated_bytes == 3, "rvalue allocator-only buffer should deallocate 3 bytes");

    ASCO_SUCCESS();
}

ASCO_TEST(buffer_reserve_preserves_content_and_cursor) {
    asco::io::buffer<> buf{5};

    ASCO_CHECK(buf.push("abc") == 3, "initial push should write 3 bytes");
    auto old_data = buf.data();
    buf.reserve(1);
    ASCO_CHECK(buf.data() == old_data, "reserve below remaining capacity should not reallocate");
    ASCO_CHECK(buf.size() == 5, "reserve below remaining capacity should keep size 5, got {}", buf.size());
    ASCO_CHECK(
        buf.cursor() == 3, "reserve below remaining capacity should keep cursor 3, got {}", buf.cursor());

    buf.reserve(2);
    ASCO_CHECK(buf.data() == old_data, "reserve equal to remaining capacity should not reallocate");
    ASCO_CHECK(buf.size() == 5, "reserve equal to remaining capacity should keep size 5, got {}", buf.size());
    ASCO_CHECK(
        buf.cursor() == 3, "reserve equal to remaining capacity should keep cursor 3, got {}", buf.cursor());

    buf.reserve(10);
    ASCO_CHECK(
        buf.size() == 13, "reserve should set size to cursor plus requested space, got {}", buf.size());
    ASCO_CHECK(buf.cursor() == 3, "reserve should preserve cursor 3, got {}", buf.cursor());
    ASCO_CHECK(view(buf) == "abc", "reserve should preserve existing bytes");

    ASCO_CHECK(buf.push("defghij") == 7, "push after reserve should use new capacity");
    ASCO_CHECK(view(buf) == "abcdefghij", "content mismatch after reserve and push");

    ASCO_SUCCESS();
}

ASCO_TEST(buffer_clear_resets_state_and_allows_reuse) {
    asco::io::buffer<> buf{8};

    ASCO_CHECK(buf.push("abc") == 3, "initial push should write 3 bytes");
    buf.clear();

    ASCO_CHECK(buf.size() == 0, "clear should reset size to 0, got {}", buf.size());
    ASCO_CHECK(buf.cursor() == 0, "clear should reset cursor to 0, got {}", buf.cursor());
    ASCO_CHECK(buf.data() == nullptr, "clear should reset data to null");

    ASCO_CHECK(buf.push("xy") == 2, "push after clear should allocate and write 2 bytes");
    ASCO_CHECK(buf.size() == 2, "push after clear should allocate size 2, got {}", buf.size());
    ASCO_CHECK(view(buf) == "xy", "push after clear content mismatch");

    ASCO_SUCCESS();
}

ASCO_TEST(buffer_move_construct_transfers_content_and_source_remains_reusable) {
    tracking_resource resource;
    tracked_allocator allocator{&resource};
    tracked_buffer source{8, allocator};

    ASCO_CHECK(source.push("move") == 4, "source push should write 4 bytes");
    auto original_data = source.data();

    tracked_buffer moved{std::move(source)};

    ASCO_CHECK(moved.data() == original_data, "move construction should transfer data pointer");
    ASCO_CHECK(moved.size() == 8, "moved buffer size should be 8, got {}", moved.size());
    ASCO_CHECK(moved.cursor() == 4, "moved buffer cursor should be 4, got {}", moved.cursor());
    ASCO_CHECK(std::memcmp(moved.data(), "move", 4) == 0, "moved buffer content mismatch");

    ASCO_CHECK(source.size() == 0, "moved-from source size should be 0, got {}", source.size());
    ASCO_CHECK(source.cursor() == 0, "moved-from source cursor should be 0, got {}", source.cursor());
    ASCO_CHECK(source.data() == nullptr, "moved-from source data should be null");

    ASCO_CHECK(source.push("ok") == 2, "moved-from source should remain reusable");
    ASCO_CHECK(source.size() == 2, "reused source size should be 2, got {}", source.size());
    ASCO_CHECK(std::memcmp(source.data(), "ok", 2) == 0, "reused source content mismatch");

    moved.clear();
    source.clear();
    ASCO_CHECK(resource.allocations == resource.deallocations, "allocator deallocation count mismatch");
    ASCO_CHECK(
        resource.allocated_bytes == resource.deallocated_bytes, "allocator deallocated byte count mismatch");

    ASCO_SUCCESS();
}

ASCO_TEST(buffer_move_assignment_releases_old_content_and_transfers_allocator) {
    tracking_resource first_resource;
    tracking_resource second_resource;
    tracked_allocator first_allocator{&first_resource};
    tracked_allocator second_allocator{&second_resource};

    tracked_buffer target{4, first_allocator};
    tracked_buffer source{7, second_allocator};
    ASCO_CHECK(target.push("old") == 3, "target push should write 3 bytes");
    ASCO_CHECK(source.push("new") == 3, "source push should write 3 bytes");

    target = std::move(source);

    ASCO_CHECK(first_resource.deallocations == 1, "move assignment should release target old storage");
    ASCO_CHECK(target.size() == 7, "target size should become 7, got {}", target.size());
    ASCO_CHECK(target.cursor() == 3, "target cursor should become 3, got {}", target.cursor());
    ASCO_CHECK(std::memcmp(target.data(), "new", 3) == 0, "target content mismatch after move assignment");

    ASCO_CHECK(source.size() == 0, "moved-from source size should be 0, got {}", source.size());
    ASCO_CHECK(source.cursor() == 0, "moved-from source cursor should be 0, got {}", source.cursor());

    target.clear();
    ASCO_CHECK(second_resource.deallocations == 1, "moved storage should be released by source allocator");

    ASCO_SUCCESS();
}

ASCO_TEST(buffer_self_move_assignment_keeps_buffer_usable) {
    asco::io::buffer<> buf{6};

    ASCO_CHECK(buf.push("self") == 4, "initial push should write 4 bytes");
    auto original_data = buf.data();

    auto *same = &buf;
    buf = std::move(*same);

    ASCO_CHECK(buf.data() == original_data, "self move should keep data pointer");
    ASCO_CHECK(buf.size() == 6, "self move should keep size 6, got {}", buf.size());
    ASCO_CHECK(buf.cursor() == 4, "self move should keep cursor 4, got {}", buf.cursor());
    ASCO_CHECK(view(buf) == "self", "self move should keep content");

    ASCO_SUCCESS();
}