// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/linux/io_uring.h>

#include <asco/rterror.h>
#include <asco/utils/math.h>

#include <cstring>
#include <format>
#include <fstream>
#include <optional>
#include <string>
#include <thread>

#include <fcntl.h>
#include <liburing.h>
#include <liburing/io_uring.h>

namespace asco::core::_linux {

static int get_numa_node(int cpu_id) {
    std::ifstream file(std::format("/sys/devices/system/cpu/cpu{}/topology/physical_package_id", cpu_id));
    int node_id = -1;
    file >> node_id;
    return node_id;
}

static ::cpu_set_t get_cpu_set_in_numa_node(int target_node) noexcept {
    ::cpu_set_t cpumask;
    CPU_ZERO(&cpumask);
    for (size_t cpu = 0; cpu < std::thread::hardware_concurrency(); cpu++) {
        if (get_numa_node(cpu) == target_node) {
            CPU_SET(cpu, &cpumask);
        }
    }
    return cpumask;
}

uring::uring(size_t worker_id)
        : worker_id(worker_id) {
    ::io_uring_params params{};
    params.sq_entries = io_uring_entries;
    params.cq_entries = io_uring_entries;
    params.flags = IORING_SETUP_SQPOLL | IORING_SETUP_SQ_AFF;
    params.sq_thread_idle = 100;
    params.sq_thread_cpu = worker_id;

    if (int err = ::io_uring_queue_init_params(io_uring_entries, &this->ring, &params); err < 0) {
        throw runtime_error(
            std::format("[ASCO] uring::uring(): Failed to initialize io_uring: {}", std::strerror(-err)));
    }

    if (auto numa_node = get_numa_node(worker_id); numa_node >= 0) {
        auto cpumask = get_cpu_set_in_numa_node(numa_node);
        if (int err = ::io_uring_register_iowq_aff(&this->ring, sizeof(cpumask), &cpumask); err < 0) {
            throw runtime_error(
                std::format(
                    "[ASCO] uring::uring(): Failed to register io_uring worker affinity: {}",
                    std::strerror(-err)));
        }
    }
}

uring::~uring() {
    while (this->iotask_count.load());
    ::io_uring_queue_exit(&this->ring);
}

std::optional<int> uring::peek(size_t seq_num) {
    ::io_uring_cqe *cqe;

    if (auto guard = this->compeleted_queue.lock(); !guard->empty()) {
        for (auto it = guard->begin(); it != guard->end(); it++) {
            if ((*it)->user_data == seq_num) {
                cqe = *it;
                guard->erase(it);
                goto finish;
            }
        }
    }

    int ret;
    do {
        cqe = nullptr;
        if (ret = ::io_uring_peek_cqe(&this->ring, &cqe);
            ret == 0 && cqe && cqe->user_data != seq_num && !(cqe->flags & IORING_CQE_F_NOTIF))
            this->compeleted_queue.lock()->push_back(cqe);
    } while (cqe && (cqe->user_data != seq_num || cqe->flags & IORING_CQE_F_NOTIF));

    if (!cqe)
        return std::nullopt;

finish:
    int res = cqe->res;
    auto ud = ::io_uring_cqe_get_data64(cqe);
    if (auto guard = unpeeked_opens.lock(); guard->contains(ud)) {
        guard->erase(ud);
    }
    ::io_uring_cqe_seen(&this->ring, cqe);
    this->iotask_count.fetch_sub(1);
    return res;
}

std::optional<io::buffer<>> uring::peek_read_buffer(size_t seq_num, size_t read) {
    if (auto guard = this->unpeeked_read_buffers.lock(); guard->contains(seq_num)) {
        auto &r = guard->at(seq_num);
        auto v = r.vec;
        auto n = r.nr_vecs;
        auto res = std::move(r).to_buffer(read);
        drop_iovec(v, n);
        guard->erase(seq_num);
        return res;
    }
    return std::nullopt;
}

std::optional<io::buffer<>> uring::peek_rest_write_buffer(size_t seq_num, size_t written) {
    if (auto guard = this->unpeeked_writes.lock(); guard->contains(seq_num)) {
        auto &w = guard->at(seq_num);
        auto buf = std::move(w.buf);
        drop_iovec(w.vec, w.nr_vecs);
        guard->erase(seq_num);

        if (buf.size() == written)
            return std::nullopt;

        auto [_, rest] = std::move(buf).split(written);
        return std::move(rest);
    }
    return std::nullopt;
}

uring::req_token uring::submit(ioreq::open req) {
    size_t reqn = this->seq_count.fetch_add(1);
    this->iotask_count.fetch_add(1);

    ::io_uring_sqe *sqe = ::io_uring_get_sqe(&this->ring);
    if (!sqe)
        throw runtime_error("[ASCO] uring::submit: Failed to get a sqe");
    ::io_uring_sqe_set_data64(sqe, reqn);
    ::open_how how{
        req.mode.raw(), static_cast<::mode_t>(req.perm),
        (req.resolve | ioreq::open::resolve_mode::in_root).raw()};

    auto [it, _] = unpeeked_opens.lock()->emplace(reqn, std::move(req));

    // God knows why openat2 always fail with -EINVAL.
    // Linux documentations are completely bullshit.
    // ::io_uring_prep_openat2(sqe, AT_FDCWD, it->second.path.c_str(), &how);
    ::io_uring_prep_openat(sqe, AT_FDCWD, it->second.path.c_str(), how.flags, how.mode);
    ::io_uring_submit(&this->ring);

    return req_token{this->worker_id, reqn};
}

uring::req_token uring::submit(ioreq::close req) {
    size_t reqn = this->seq_count.fetch_add(1);
    this->iotask_count.fetch_add(1);

    ::io_uring_sqe *sqe = ::io_uring_get_sqe(&this->ring);
    if (!sqe)
        throw runtime_error("[ASCO] uring::submit: Failed to get a sqe");
    ::io_uring_sqe_set_data64(sqe, reqn);

    ::io_uring_prep_close(sqe, req.fd);
    ::io_uring_submit(&this->ring);

    return req_token{this->worker_id, reqn};
}

uring::req_token uring::submit(ioreq::write req) {
    size_t reqn = this->seq_count.fetch_add(1);
    this->iotask_count.fetch_add(1);

    ::io_uring_sqe *sqe = ::io_uring_get_sqe(&this->ring);
    if (!sqe)
        throw runtime_error("[ASCO] uring::submit: Failed to get a sqe");
    ::io_uring_sqe_set_data64(sqe, reqn);

    req.nr_vecs = req.buf.buffer_count();
    req.vec = get_iovec(req.nr_vecs);
    for (auto it = req.buf.rawbuffers(); !it.is_end(); it++) {
        auto b = *it;
        req.vec[b.seq] = ::iovec{b.buffer, b.size * sizeof(decltype(req.buf)::char_type)};
    }

    ::io_uring_prep_writev2(sqe, req.fd, req.vec, req.nr_vecs, req.offset, 0);
    ::io_uring_sqe_set_flags(sqe, IOSQE_ASYNC);
    ::io_uring_submit(&this->ring);
    unpeeked_writes.lock()->emplace(reqn, std::move(req));

    return req_token{this->worker_id, reqn};
}

uring::req_token uring::submit(ioreq::read req) {
    size_t reqn = this->seq_count.fetch_add(1, morder::seq_cst);
    this->iotask_count.fetch_add(1);

    ::io_uring_sqe *sqe = ::io_uring_get_sqe(&this->ring);
    if (!sqe)
        throw runtime_error("[ASCO] uring::submit: Failed to get a sqe");
    ::io_uring_sqe_set_data64(sqe, reqn);

    size_t nr_vecs = (req.nbytes + uring::read_buffer::unit_size - 1) / uring::read_buffer::unit_size;
    read_buffers_iovec buf{nr_vecs, get_iovec(nr_vecs)};
    for (size_t i{0}; i < nr_vecs; i++) {
        buf.vec[i] = ::iovec{new uring::read_buffer, std::min(uring::read_buffer::unit_size, req.nbytes)};
        req.nbytes -= uring::read_buffer::unit_size;
    }
    unpeeked_read_buffers.lock()->emplace(reqn, buf);

    ::io_uring_prep_readv2(sqe, req.fd, buf.vec, buf.nr_vecs, req.offset, 0);
    ::io_uring_sqe_set_flags(sqe, IOSQE_ASYNC);
    ::io_uring_submit(&this->ring);

    return req_token{this->worker_id, reqn};
}

thread_local spin<slub::object<uring::read_buffer> *> uring::read_buffer::slub;

void *uring::read_buffer::operator new(size_t) noexcept {
    if (auto guard = slub.lock(); *guard) {
        auto res = *guard;
        *guard = res->next;
        return res->obj();
    }

    return new char[unit_size];
}

void uring::read_buffer::operator delete(void *ptr_) noexcept {
    auto ptr = static_cast<read_buffer *>(ptr_);
    auto obj = slub::object<read_buffer>::from(ptr);

    with(auto guard = slub.lock()) {
        if (*guard && (*guard)->len == slub_max) {
            delete[] static_cast<char *>(ptr_);
        }

        obj->next = *guard;
        if (*guard)
            obj->len = (*guard)->len + 1;
        *guard = obj;
    }
}

void uring::read_buffer::buffer_destroyer(char *ptr) noexcept { delete reinterpret_cast<read_buffer *>(ptr); }

io::buffer<> uring::read_buffers_iovec::to_buffer(this uring::read_buffers_iovec &&self, size_t size) {
    io::buffer<> res;
    for (size_t i{0}; i < self.nr_vecs; i++) {
        res.push_raw_array_buffer(
            reinterpret_cast<char *>(self.vec[i].iov_base), uring::read_buffer::unit_size,
            std::min(uring::read_buffer::unit_size, size), &uring::read_buffer::buffer_destroyer);
        self.vec[i].iov_base = nullptr;
        self.vec[i].iov_len = 0;

        size -= uring::read_buffer::unit_size;
    }

    self.nr_vecs = 0;
    return res;
}

::iovec *uring::get_iovec(size_t len_) noexcept {
    auto len = min_exp2_from(len_);

    if (auto guard = this->iovec_cache.lock(); guard->contains(len)) {
        auto res = guard->at(len);
        auto next = res->next;
        if (!next) {
            guard->erase(len);
        } else {
            guard->at(len) = next;
        }
        return res->obj();
    }

    return new iovec[len];
}

void uring::drop_iovec(::iovec *ptr, size_t len_) noexcept {
    auto obj = slub::object<::iovec>::from(ptr);
    auto len = min_exp2_from(len_);

    if (auto guard = this->iovec_cache.lock(); guard->contains(len)) {
        auto &o = guard->at(len);
        if (o->len > iovec_cache_unit_max) {
            delete[] ptr;
        } else {
            obj->next = o;
            obj->len = o->len + 1;
            o = obj;
        }
    } else {
        obj->next = nullptr;
        obj->len = 1;
        guard->emplace(len, obj);
    }
}

};  // namespace asco::core::_linux
