// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#ifndef ASCO_UTILS_PLACEMENT_LIST_H
#define ASCO_UTILS_PLACEMENT_LIST_H 1

#include <asco/sync/rwspin.h>
#include <asco/sync/spin.h>
#include <asco/utils/pubusing.h>

#include <cassert>
#include <new>

#ifdef offsetof
#    define tmp_offsetof(type, member) offsetof(type, member)
#    undef offsetof
#endif
#define offsetof(type, member) (asco::types::size_t)(&((type *)0)->member)

namespace asco::placement_list {

using namespace types;

struct concurrency {};

struct passcheck {};

template<typename Con>
concept optional_concurrency = std::is_void_v<Con> || std::is_same_v<Con, concurrency>;

template<typename T, optional_concurrency Con = void>
struct containee;

template<typename T, optional_concurrency Con = void>
class node;

template<typename T, typename Con = void>
concept placement_list = requires(T t) {
    { t.__list_node } -> std::same_as<containee<T, Con> &>;
};

template<typename T>
class node<T, void> {
    static_assert(
        std::is_same_v<T, passcheck> || placement_list<T, void> || placement_list<T, concurrency>,
        "[ASCO] placement_list::node<T, void>: T must be a placement_list");

public:
    class head {
        friend class node;

    public:
        class iterator {
        public:
            iterator(node *current, node *last) noexcept
                    : current{current}
                    , last{last} {}

            const T *operator->() const noexcept { return current->container(); }
            T *operator->() noexcept { return current->container(); }

            const T &operator*() const noexcept { return *current->container(); }
            T &operator*() noexcept { return *current->container(); }

            iterator &operator++() noexcept {
                current = (current == last || current == nullptr) ? nullptr : current->next();
                return *this;
            }

            bool operator!=(const iterator &other) const noexcept { return current != other.current; }

            node *get_node() noexcept { return current; }
            const node *get_node() const noexcept { return current; }

        private:
            node *current;
            const node *last;
        };

        head() noexcept = default;

        head(node *list, size_t length) noexcept
                : _head{list} {
            size_t count = 0;
            for (auto p = list; p && count < length; p = p->_next) {
                p->_list_handle = this;
                count++;
                if (!p->_next)
                    _tail.store(p, morder::relaxed);
            }
            if (list) {
                assert(count == length && "[ASCO] placement_list::head: length mismatch with actual chain");
                _length.store(count, morder::relaxed);
            } else {
                assert(length == 0);
                _length.store(0, morder::relaxed);
            }
        }

        iterator begin() noexcept {
            return iterator(_head.load(morder::relaxed), _tail.load(morder::relaxed));
        }
        const iterator begin() const noexcept {
            return iterator(_head.load(morder::relaxed), _tail.load(morder::relaxed));
        }

        iterator end() noexcept { return iterator(nullptr, _tail.load(morder::relaxed)); }
        const iterator end() const noexcept { return iterator(nullptr, _tail.load(morder::relaxed)); }

        void push_front(T *ptr) noexcept {
            auto n = placement_list::node<T>::from(ptr);
            push_front(n);
        }

        void push_front(node *n) noexcept {
            assert(n->_list_handle == nullptr);
            if (!_head.load(morder::relaxed)) {
                n->_prev = nullptr;
                n->_next = nullptr;
                n->_list_handle = this;
                _head.store(n, morder::relaxed);
                _tail.store(n, morder::relaxed);
                _length.fetch_add(1, morder::relaxed);
            } else {
                _head.store(_head.load(morder::relaxed)->insert(n), morder::relaxed);
            }
        }

        void push_back(T *ptr) noexcept {
            auto n = placement_list::node<T>::from(ptr);
            push_back(n);
        }

        void push_back(node *n) noexcept {
            assert(n->_list_handle == nullptr);
            if (!_tail.load(morder::relaxed)) {
                n->_prev = nullptr;
                n->_next = nullptr;
                n->_list_handle = this;
                _head.store(n, morder::relaxed);
                _tail.store(n, morder::relaxed);
                _length.fetch_add(1, morder::relaxed);
                return;
            }
            auto t = _tail.load(morder::relaxed);
            n->_prev = t;
            n->_next = nullptr;
            n->_list_handle = this;
            t->_next = n;
            _tail.store(n, morder::relaxed);
            _length.fetch_add(1, morder::relaxed);
        }

        T *pop_front() noexcept {
            if (!_head.load(morder::relaxed))
                return nullptr;

            auto h = _head.load(morder::relaxed);
            auto res = h->container();
            auto p = h->_next;
            h->remove();
            _head.store(p, morder::relaxed);
            if (!_head.load(morder::relaxed))
                _tail.store(nullptr, morder::relaxed);
            return res;
        }

        T *pop_back() noexcept {
            if (!_tail.load(morder::relaxed))
                return nullptr;

            auto h = _tail.load(morder::relaxed);
            auto res = h->container();
            auto p = h->_prev;
            h->remove();
            _tail.store(p, morder::relaxed);
            if (!_tail.load(morder::relaxed))
                _head.store(nullptr, morder::relaxed);
            return res;
        }

        size_t length() const noexcept { return _length.load(morder::relaxed); }

    protected:
        atomic<node *> _head{nullptr};
        atomic<node *> _tail{nullptr};
        atomic_size_t _length{0};
    };

    static node<T> *from(T *ptr) noexcept {
        return reinterpret_cast<node *>(reinterpret_cast<char *>(ptr) + offsetof(T, __list_node));
    }

    T *container() noexcept {
        return reinterpret_cast<T *>(reinterpret_cast<char *>(this) - offsetof(T, __list_node));
    }

    const T *container() const noexcept {
        return reinterpret_cast<const T *>(reinterpret_cast<const char *>(this) - offsetof(T, __list_node));
    }

    // Insert new node before this node.
    // Returns the inserted node.
    node *insert(node *n) noexcept {
        assert(!n->_list_handle);

        n->_prev = _prev;
        n->_next = this;
        if (_prev)
            _prev->_next = n;
        _prev = n;

        n->_list_handle = _list_handle;
        if (_list_handle->_head.load(morder::relaxed) == this)
            _list_handle->_head.store(n, morder::relaxed);
        _list_handle->_length.fetch_add(1, morder::relaxed);

        return n;
    }

    node *remove() noexcept {
        auto *h = _list_handle;
        node *prev = _prev;
        node *next = _next;
        if (prev)
            prev->_next = next;
        if (next)
            next->_prev = prev;
        if (h) {
            if (h->_head.load(morder::relaxed) == this)
                h->_head.store(next, morder::relaxed);
            if (h->_tail.load(morder::relaxed) == this)
                h->_tail.store(prev, morder::relaxed);
            h->_length.fetch_sub(1, morder::relaxed);
            _list_handle = nullptr;
        }
        _prev = nullptr;
        _next = nullptr;
        return this;
    }

    node *next() const noexcept { return _next; }
    node *prev() const noexcept { return _prev; }

    head *get_head() const noexcept { return _list_handle; }

protected:
    node *_next{nullptr};
    node *_prev{nullptr};

    head *_list_handle{nullptr};
};

template<typename T, optional_concurrency Con>
class node : public node<T, void> {
    static_assert(
        std::is_same_v<Con, concurrency> || std::is_void_v<Con>, "[ASCO] unsupported concurrency tag");

    using base_node = node<T, void>;
    using base_head = base_node::head;

public:
    using base_node::base_node;
    using base_node::container;
    using base_node::from;
    using base_node::next;
    using base_node::prev;

    class head : public base_head {
    public:
        using base_head::base_head;

        head() noexcept = default;

        class iterator {
        public:
            iterator(node *current, node *last) noexcept
                    : current{current}
                    , last{last} {
                lock_current();
            }

            iterator(const iterator &) = delete;
            iterator &operator=(const iterator &) = delete;
            iterator(iterator &&) noexcept = delete;

            ~iterator() { unlock_current(); }

            const T *operator->() const noexcept { return current->container(); }
            T *operator->() noexcept { return current->container(); }

            const T &operator*() const noexcept { return *current->container(); }
            T &operator*() noexcept { return *current->container(); }

            iterator &operator++() noexcept {
                unlock_current();
                current =
                    (current == last || current == nullptr) ? nullptr : static_cast<node *>(current->_next);
                lock_current();
                return *this;
            }

            bool operator!=(const iterator &other) const noexcept { return current != other.current; }

            node *get_node() noexcept { return current; }
            const node *get_node() const noexcept { return current; }

        private:
            void lock_current() noexcept {
                if (!current)
                    return;
                new (guard) rwspin<>::write_guard(current->_container_lock.write());
                locked = true;
            }

            void unlock_current() noexcept {
                if (!locked)
                    return;
                reinterpret_cast<guard_t *>(guard)->~guard_t();
                locked = false;
            }

            node *current;
            const node *last;

            using guard_t = decltype(current->_container_lock.write());

            alignas(guard_t) char guard[sizeof(guard_t)];
            bool locked{false};
        };

        class const_iterator {
        public:
            const_iterator(node *current, node *last) noexcept
                    : current{current}
                    , last{last} {
                lock_current();
            }

            const_iterator(const const_iterator &) = delete;
            const_iterator &operator=(const const_iterator &) = delete;
            const_iterator(iterator &&) noexcept = delete;

            ~const_iterator() { unlock_current(); }

            const T *operator->() const noexcept { return current->container(); }

            const T &operator*() const noexcept { return *current->container(); }

            const_iterator &operator++() noexcept {
                unlock_current();
                current = (current == last || current == nullptr) ? nullptr : current->_next;
                lock_current();
                return *this;
            }

            bool operator!=(const const_iterator &other) const noexcept { return current != other.current; }

            node *get_node() noexcept { return current; }
            const node *get_node() const noexcept { return current; }

        private:
            void lock_current() noexcept {
                if (!current)
                    return;
                new (guard) rwspin<>::read_guard(current->_container_lock.read());
                locked = true;
            }

            void unlock_current() noexcept {
                if (!locked)
                    return;
                reinterpret_cast<guard_t *>(guard)->~guard_t();
                locked = false;
            }

            node *current;
            const node *last;

            using guard_t = decltype(current->_container_lock.read());

            alignas(guard_t) char guard[sizeof(guard_t)];
            bool locked{false};
        };

        iterator begin() noexcept {
            return iterator(
                static_cast<node *>(this->_head.load(morder::acquire)),
                static_cast<node *>(this->_tail.load(morder::acquire)));
        }

        const_iterator begin() const noexcept {
            return const_iterator(
                static_cast<node *>(this->_head.load(morder::acquire)),
                static_cast<node *>(this->_tail.load(morder::acquire)));
        }

        iterator end() noexcept {
            return iterator(nullptr, static_cast<node *>(this->_tail.load(morder::acquire)));
        }

        const_iterator end() const noexcept {
            return const_iterator(nullptr, static_cast<node *>(this->_tail.load(morder::acquire)));
        }

        void push_front(T *ptr) noexcept { push_front(node::from(ptr)); }

        void push_back(T *ptr) noexcept { push_back(node::from(ptr)); }

        void push_front(node *n) noexcept {
            assert(n->_list_handle == nullptr);
            while (true) {
                auto old_head = static_cast<node *>(this->_head.load(morder::acquire));
                if (!old_head) {
                    // empty list fast path
                    auto lg = n->_link_lock.lock();
                    if (this->_head.load(morder::acquire))
                        continue;  // lost race
                    n->_prev = n->_next = nullptr;
                    n->_list_handle = this;
                    this->_head.store(n, morder::release);
                    this->_tail.store(n, morder::release);
                    this->_length.fetch_add(1, morder::release);
                    return;
                }
                old_head->insert(n);
                return;
            }
        }

        void push_back(node *n) noexcept {
            assert(n->_list_handle == nullptr);
            for (;;) {
                auto old_tail = static_cast<node *>(this->_tail.load(morder::acquire));
                if (!old_tail) {
                    auto lg = n->_link_lock.lock();
                    if (this->_tail.load(morder::acquire) != nullptr)
                        continue;  // raced, retry as non-empty path
                    n->_prev = n->_next = nullptr;
                    n->_list_handle = this;
                    this->_head.store(n, morder::release);
                    this->_tail.store(n, morder::release);
                    this->_length.fetch_add(1, morder::release);
                    return;
                }
                auto lg_new = n->_link_lock.lock();
                auto lg_tail = old_tail->_link_lock.lock();
                if (this->_tail.load(morder::acquire) != old_tail)
                    continue;  // tail changed, retry
                n->_next = nullptr;
                n->_prev = old_tail;
                old_tail->_next = n;
                n->_list_handle = this;
                this->_tail.store(n, morder::release);
                this->_length.fetch_add(1, morder::release);
                return;
            }
        }

        T *pop_front() noexcept {
            while (true) {
                auto h = static_cast<node *>(this->_head.load(morder::acquire));
                if (!h)
                    return nullptr;
                // try to ensure we remove current head; if a new node inserted before h, retry
                if (h->_prev) {
                    continue;  // not current head anymore
                }
                auto *val = h->container();
                h->remove();
                return val;
            }
        }

        T *pop_back() noexcept {
            while (true) {
                auto t = static_cast<node *>(this->_tail.load(morder::acquire));
                if (!t)
                    return nullptr;
                if (t->_next) {
                    continue;  // not tail anymore
                }
                auto *val = t->container();
                t->remove();
                return val;
            }
        }

        size_t length() const noexcept { return this->_length.load(morder::acquire); }
    };

    static node *from(T *ptr) noexcept {
        return reinterpret_cast<node *>(reinterpret_cast<char *>(ptr) + offsetof(T, __list_node));
    }

    node *insert(node *n) noexcept {
        assert(n && n->_list_handle == nullptr);

        auto prev = static_cast<node *>(this->_prev);
        node *order[3] = {prev, this, n};
        std::sort(order, order + 3);

        spin<>::guard *locks[3]{nullptr, nullptr, nullptr};
        alignas(spin<>::guard) char buf[3][sizeof(spin<>::guard)];

        size_t li = 0;
        for (auto *t : order) {
            if (!t)
                continue;
            auto i = li++;
            locks[i] = new (&buf[i]) spin<>::guard(t->_link_lock.lock());
        }

        base_node::insert(n);

        for (int k = li - 1; k >= 0;) {
            if (locks[k]) {
                locks[k]->~guard();
                k--;
            }
        }

        return n;
    }

    node *remove() noexcept {
        if (!this->_list_handle)
            return this;

        auto prev = static_cast<node *>(this->_prev);
        auto next = static_cast<node *>(this->_next);
        node *order[3] = {prev, this, next};
        std::sort(order, order + 3);

        spin<>::guard *locks[3]{nullptr, nullptr, nullptr};
        alignas(spin<>::guard) char buf[3][sizeof(spin<>::guard)];

        size_t li = 0;
        for (auto *t : order) {
            if (!t)
                continue;
            auto i = li++;
            locks[i] = new (&buf[i]) spin<>::guard(t->_link_lock.lock());
        }

        base_node::remove();

        for (int k = li - 1; k >= 0;) {
            if (locks[k]) {
                locks[k]->~guard();
                k--;
            }
        }

        return this;
    }

private:
    spin<> _link_lock;
    rwspin<> _container_lock;
};

template<typename T, optional_concurrency Con>
struct containee {
    char c[sizeof(node<passcheck, Con>)];

    containee() noexcept { new (&c) node<T, Con>(); }
    ~containee() noexcept { reinterpret_cast<node<T, Con> *>(&c)->~node(); }
};

template<typename T, optional_concurrency Con = void>
using head = typename node<T, Con>::head;

};  // namespace asco::placement_list

#undef offsetof
#ifdef tmp_offsetof
#    define offsetof(type, member) tmp_offsetof(type, member)
#endif

#endif
