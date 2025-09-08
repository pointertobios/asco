// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_SLUB_H
#define ASCO_SLUB_H 1

#include <asco/perf.h>
#include <asco/sync/rwspin.h>
#include <asco/utils/concurrency.h>
#include <asco/utils/placement_list.h>
#include <asco/utils/pubusing.h>

#include <cassert>
#include <chrono>

namespace asco::core::slub {

using namespace types;
using namespace std::chrono_literals;

using concurrency::atomic_ptr;

template<size_t A>
inline constexpr size_t align_to(size_t size) noexcept {
    return (size + (A - 1)) & ~(A - 1);
}

template<size_t A>
inline constexpr bool is_aligned(void *addr) noexcept {
    return (reinterpret_cast<size_t>(addr) & (A - 1)) == 0;
}

template<typename T>
struct object {
    object<T> *next;
    size_t len;
    char padding[align_to<16>(sizeof(T)) - sizeof(len) - sizeof(next)];

    static object<T> *from(T *ptr) noexcept { return reinterpret_cast<object<T> *>(ptr); }

    T *obj() noexcept { return reinterpret_cast<T *>(this); }
};

template<typename T>
class local_cache;

template<typename T>
class cache;

template<typename T>
class page {
public:
    constexpr static size_t size = 4096;
    constexpr static size_t obj_size = align_to<16>(sizeof(T));
    constexpr static size_t largest_obj_size = 1024;

    static_assert(sizeof(T) <= largest_obj_size, "[ASCO] SLUB only supports object size <= 1024 bytes.");

    struct group {
        const object<page> *base;
        object<page> *freelist;
        const size_t size;

        placement_list::containee<group, placement_list::concurrency> __list_node;

        group(object<page> *base, size_t size)
                : base{base}
                , freelist{base}
                , size{size} {
            for (auto obj = freelist, end = freelist + size; obj < end; obj++) {
                obj->next = (obj + 1 == end) ? nullptr : (obj + 1);
                obj->len = end - obj;
            }
        }

        page *pop_page() noexcept {
            auto res = freelist;
            if (freelist)
                freelist = freelist->next;
            return res->obj();
        }

        void push_page(page *ptr) noexcept {
            auto obj = object<page>::from(ptr);
            assert(obj - base < static_cast<ssize_t>(size));

            obj->next = freelist;
            obj->len = freelist ? freelist->len + 1 : 1;
            freelist = obj;
        }

        bool is_full() const noexcept { return !freelist; }
    };

public:
    class guard {
    public:
        guard(page *p) noexcept
                : p{p}
                , g{p->page_lock.write()} {}

        void deallocate(T *ptr) noexcept { p->deallocate(ptr); }

    private:
        page *p;
        sync::rwspin<>::write_guard g;
    };

    page(group *page_group) noexcept
            : page_group{page_group}
            , freelist{reinterpret_cast<object<T> *>(reinterpret_cast<char *>(this) + meta_size)} {
        for (auto obj = freelist, end = freelist + obj_max; obj < end; obj++) {
            obj->next = (obj + 1 == end) ? nullptr : (obj + 1);
            obj->len = end - obj;
        }
    }

    static page *from_object(object<T> *obj) noexcept {
        auto addr = reinterpret_cast<uintptr_t>(obj);
        return reinterpret_cast<page *>(addr & ~(size - 1));
    }

    void *allocate() noexcept {
        assert(is_aligned<page<T>::size>(this));

        auto _ = page_lock.read();

        auto res = freelist;
        if (freelist)
            freelist = freelist->next;
        return res->obj();
    }

    void deallocate(T *ptr) noexcept {
        assert(contains(ptr));
        assert(is_aligned<page<T>::size>(this));

        auto _ = page_lock.read();

        deallocate_impl(ptr);
    }

    bool contains(void *ptr) const noexcept {
        char *p = reinterpret_cast<char *>(ptr);
        const char *start = reinterpret_cast<const char *>(this) + meta_size;
        return p >= start && p - start < static_cast<ssize_t>(size - meta_size);
    }

    bool is_full() const noexcept { return !freelist; }
    bool is_empty() const noexcept { return freelist ? freelist->len == obj_max : false; }

    guard cross_lock() noexcept { return {this}; }

    placement_list::containee<page, placement_list::concurrency> __list_node;

    group *page_group;

private:
    object<T> *freelist;

    // Usually, one page would only used in a single thread.
    // But local_cache can give partial page to the global cache.
    // It is a little bit likely racing on freelist.
    // So we use a rwspin. Only when calling cross_lock() this lock will be lock with read().
    sync::rwspin<> page_lock;

    char padding[size - sizeof(__list_node) - sizeof(group *) - sizeof(object<T> *) - sizeof(page_lock)];

    constexpr static size_t meta_size = align_to<16>(size - sizeof(padding));
    constexpr static size_t obj_max = (size - meta_size) / obj_size;

    void deallocate_impl(T *ptr) noexcept {
        assert(contains(ptr));
        assert(is_aligned<page<T>::size>(this));

        auto obj = object<T>::from(ptr);
        obj->next = freelist;
        obj->len = freelist ? freelist->len + 1 : 1;
        freelist = obj;
    }
};

template<typename T>
class local_cache {
private:
    template<typename U>
    using node = placement_list::node<U>;

    constexpr static size_t object_cache_max = 8192;
    constexpr static size_t pages_max = 64;

public:
    local_cache() noexcept = default;

    void *allocate() noexcept {
        if (freelist) {
            auto res = freelist;
            freelist = freelist->next;
            return res->obj();
        }

        if (current_page && !current_page->is_full()) {
            auto res = current_page->allocate();
            if (current_page->is_full()) {
                pages.push_back(current_page);
                current_page = nullptr;
            }
            return res;
        }

        if (!pages.begin()->is_full()) {
            current_page = pages.pop_front();
            auto res = current_page->allocate();
            if (current_page->is_full()) {
                pages.push_back(current_page);
                current_page = nullptr;
            }
            return res;
        }

        return nullptr;
    }

    // Return one node in `pages` 's tail if `pages` 's length is greater than `pages_max`
    page<T> *deallocate(T *ptr) noexcept {
        auto obj = object<T>::from(ptr);

        if (!freelist || freelist->len < object_cache_max) {
            obj->next = freelist;
            obj->len = freelist ? freelist->len + 1 : 1;
            freelist = obj;
            return nullptr;
        }

        if (current_page && current_page->contains(ptr)) {
            current_page->deallocate(ptr);
            return nullptr;
        }

        auto p = page<T>::from_object(obj);
        auto n = node<page<T>>::from(p);
        assert(n->get_head() == &pages);

        p->deallocate(ptr);
        n->remove();
        pages.push_front(n);

        if (pages.length() > pages_max)
            return pages.pop_back();
        else
            return nullptr;
    }

    bool allocable() const noexcept {
        return freelist || (current_page && !current_page->is_full())
               || (pages.length() > 0 && !pages.begin()->is_full());
    }

    bool contains_page(page<T> *p) const noexcept {
        return p == current_page || node<page<T>>::from(p)->get_head() == &pages;
    }

    void push_page(page<T> *n) noexcept {
        if (!n->is_full()) [[likely]] {
            pages.push_front(n);
        } else {
            pages.push_back(n);
        }
    }

private:
    object<T> *freelist{nullptr};

    page<T> *current_page{nullptr};

    placement_list::head<page<T>> pages{};
};

template<typename T>
class cache {
private:
    using clock = std::chrono::steady_clock;

    using concurrency = placement_list::concurrency;
    template<typename U>
    using plmlist_node = placement_list::node<U, concurrency>;
    template<typename U>
    using plmlist_head = placement_list::head<U, concurrency>;

    struct alignas(page<T>::size) base_alloc_struct {
        char data[page<T>::size];
    };

    constexpr static size_t empty_pages_max = 256;
    constexpr static size_t object_cache_max = 4096;

public:
    constexpr cache() noexcept = default;

    void *allocate() noexcept {
        if (local.allocable())
            return local.allocate();

        {
            typename atomic_ptr<object<T>>::versioned_ptr res;
            object<T> *next;
            do {
                res = freelist.load(morder::acquire);
                next = res ? res->next : nullptr;
            } while (!freelist.compare_exchange_weak(res, next, morder::acq_rel, morder::acquire));
            if (res)
                return res->obj();
        }

        if (partial_pages.length() > 0) {
            auto p = partial_pages.pop_front();
            auto res = p->allocate();
            local.push_page(p);
            return res;
        }

        if (empty_pages.length() > 0) {
            auto p = empty_pages.pop_front();
            auto res = p->allocate();
            local.push_page(p);
            return res;
        }

        if (auto beg = cached_groups.begin(); beg != cached_groups.end() && !beg->is_full()) {
            auto p = beg->pop_page();
            auto res = p->allocate();
            local.push_page(p);
            return res;
        }

        auto now = clock::now();
        if (auto dur = now - last_alloc_time; dur < 10ms)
            once_alloc_pages_exp++;
        else if (dur > 1s && once_alloc_pages_exp > 0)
            once_alloc_pages_exp--;
        last_alloc_time = now;

        auto sz = 1ull << once_alloc_pages_exp;
        auto p = reinterpret_cast<object<page<T>> *>(new base_alloc_struct[sz]);
        auto group = new page<T>::group{p, sz};
        cached_groups.push_front(group);

        while (auto p = group->pop_page()) {
            auto new_page = new (p) page<T>{group};
            empty_pages.push_front(new_page);
        }

        auto pg = empty_pages.pop_front();
        auto res = pg->allocate();
        local.push_page(pg);
        return res;
    }

    void deallocate(T *ptr) noexcept {
        auto obj = object<T>::from(ptr);
        auto curr_page = page<T>::from_object(obj);

        if (local.contains_page(curr_page)) {
            auto rare_page = local.deallocate(ptr);
            if (!rare_page)
                return;

            if (rare_page->is_empty())
                empty_pages.push_back(rare_page);
            else if (rare_page->is_full())
                full_pages.push_back(rare_page);
            else
                partial_pages.push_back(rare_page);
            return;
        }

        {
            typename atomic_ptr<object<T>>::versioned_ptr old_head;
            do {
                old_head = freelist.load(morder::acquire);
                if (old_head && old_head->len >= object_cache_max)
                    break;
                obj->next = old_head;
                obj->len = old_head ? old_head->len + 1 : 1;
            } while (({
                auto cond = !freelist.compare_exchange_weak(old_head, obj, morder::acq_rel, morder::acquire);
                if (!cond)
                    return;
                cond;
            }));
        }

        curr_page->cross_lock().deallocate(ptr);

        auto curr_node = plmlist_node<page<T>>::from(curr_page);
        if (curr_node->get_head() == &full_pages) {
            curr_node->remove();
            partial_pages.push_front(curr_page);
        } else if (curr_node->get_head() == &partial_pages) {
            if (curr_page->is_empty()) {
                curr_node->remove();
                empty_pages.push_front(curr_page);
            }
        }

        if (empty_pages.length() > empty_pages_max) {
            auto p = empty_pages.pop_back();
            auto group = p->page_group;
            group->push_page(p);

            if (group->freelist && group->freelist->len == group->size) {
                auto gnode = plmlist_node<typename page<T>::group>::from(group);
                gnode->remove();
                delete[] reinterpret_cast<const base_alloc_struct *>(group->base);
                delete group;
            }
        }
    }

private:
    thread_local static local_cache<T> local;

    atomic_ptr<object<T>> freelist{};

    plmlist_head<page<T>> empty_pages{};    // All objects are not allocated
    plmlist_head<page<T>> partial_pages{};  // Some objects are allocated
    plmlist_head<page<T>> full_pages{};     // All objects are allocated

    // If alloc page another time in 10ms, double this(+1).
    // If alloc page another time out of 1000ms, half this(-1).
    atomic_uint8_t once_alloc_pages_exp{2};
    clock::time_point last_alloc_time{};

    plmlist_head<typename page<T>::group> cached_groups{};
};

template<typename T>
inline thread_local local_cache<T> cache<T>::local{};

};  // namespace asco::core::slub

#endif
