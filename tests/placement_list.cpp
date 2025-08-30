// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <cassert>
#include <iostream>

#include <asco/future.h>
#include <asco/time/sleep.h>
#include <asco/utils/placement_list.h>

using asco::future;

struct plist_node {
    int value{0};
    asco::placement_list::containee<plist_node> __list_node{};  // intrusive hook
};

struct plist_node_conc {
    int value{0};
    asco::placement_list::containee<plist_node_conc, asco::placement_list::concurrency> __list_node{};
};

static void test_single_thread_basic() {
    using namespace asco::placement_list;
    node<plist_node>::head h;
    plist_node a{1}, b{2}, c{3};

    h.push_back(&a);
    h.push_front(&b);  // b, a
    h.push_back(&c);   // b, a, c

    assert(h.length() == 3);
    int seq[] = {b.value, a.value, c.value};
    int i = 0;
    for (auto &e : h) {
        assert(e.value == seq[i]);
        i++;
    }
    assert(i == 3);

    auto *front = h.pop_front();  // remove b -> a, c
    assert(front && front->value == 2);
    assert(h.length() == 2);
    auto *back = h.pop_back();  // remove c -> a
    assert(back && back->value == 3);
    assert(h.length() == 1);
    auto *last = h.pop_front();
    assert(last && last->value == 1);
    assert(h.length() == 0);
    assert(h.pop_front() == nullptr);
}

static void test_insert_remove_middle() {
    using namespace asco::placement_list;
    plist_node n1{1}, n2{2}, n3{3}, n4{4};
    node<plist_node>::head h;
    h.push_back(&n1);  // 1
    h.push_back(&n3);  // 1,3
    auto *n3node = node<plist_node>::from(&n3);
    n3node->insert(node<plist_node>::from(&n2));  // insert before 3 -> 1,2,3
    auto *n1node = node<plist_node>::from(&n1);
    n1node->insert(node<plist_node>::from(&n4));  // insert before 1 (becomes new head) -> 4,1,2,3
    int expect[] = {4, 1, 2, 3};
    int i = 0;
    for (auto &e : h) {
        assert(e.value == expect[i]);
        i++;
    }
    assert(i == 4);

    node<plist_node>::from(&n1)->remove();  // list 4,2,3
    node<plist_node>::from(&n3)->remove();  // list 4,2
    int expect2[] = {4, 2};
    i = 0;
    for (auto &e : h) {
        assert(e.value == expect2[i]);
        i++;
    }
    assert(h.length() == 2);
}

static void test_concurrency_head_basic() {
    using namespace asco::placement_list;
    node<plist_node_conc, concurrency>::head h;
    plist_node_conc a{10}, b{20}, c{30};
    h.push_front(&a);  // a
    h.push_front(&b);  // b,a
    h.push_back(&c);   // b,a,c
    assert(h.length() == 3);
    int total = 0;
    for (auto it = h.begin(); it != h.end(); ++it) { total += it->value; }
    assert(total == 60);
    auto *p = h.pop_front();
    assert(p && p->value == 20);
    p = h.pop_back();
    assert(p && p->value == 30);
    p = h.pop_front();
    assert(p && p->value == 10);
    assert(h.length() == 0);
}

future<int> async_main() {
    test_single_thread_basic();
    test_insert_remove_middle();
    test_concurrency_head_basic();
    std::cout << "placement_list tests passed" << std::endl;
    co_return 0;
}
