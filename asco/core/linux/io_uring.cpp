// Copyright (C) 2025 pointer-to-bios <pointer-to-bios@outlook.com>
// SPDX-License-Identifier: MIT

#include <asco/core/linux/io_uring.h>

#include <asco/core/runtime.h>
#include <asco/rterror.h>
#include <asco/utils/concurrency.h>
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
#include <sys/eventfd.h>

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

bool uring_poll_thread::attach(
    size_t seq_num, sched::task::task_id tid,
    spin<std::unordered_map<size_t, sched::task::task_id>>::guard guard) {
    if (auto compg = completed_cqes.lock(); compg->contains(seq_num))
        return false;
    (*guard)[seq_num] = tid;
    return true;
}

bool uring_poll_thread::initialize(atomic_bool &running) {
    while (running.load(morder::acquire) && event_fd.load(morder::acquire) < 0) concurrency::cpu_relax();
    if (!running.load(morder::acquire))
        return false;

    pfds[0].fd = event_fd.load(morder::acquire);
    pfds[0].events = POLLIN;

    return true;
}

void uring_poll_thread::run() {
    int res = ::poll(pfds, 1, 1000);

    // FIXME Sometimes poll never returns. Sometimes the attaching tasks never be awaken.
    // So we use 1000ms timeout and awake a attaching task some time.
    // One day we find the reason, fix it.
    if (auto guard = attaching_tasks.lock(); !guard->empty()) {
        auto it = guard->begin();
        auto [seq_num, tid] = *it;
        guard->erase(it);
        if (RT::__worker::task_available(tid)) {
            auto &w = RT::__worker::get_worker_from_task_id(tid);
            w.sc.awake(tid);
            w.awake();
        }
    }

    if (res < 0 && errno == EINTR) {
        return;
    }

    if (!(pfds[0].revents & POLLIN))
        return;

    uint64_t count;
    while (int err = ::read(this->event_fd, &count, sizeof(count))) {
        if (err < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            throw runtime_error(
                std::format("[ASCO] uring::poll_thread(): read() failed: {}", std::strerror(errno)));
        } else if (err >= 0) {
            break;
        }
    }
    unhandled_cqe_count.fetch_add(count, morder::acq_rel);

    ::io_uring_cqe *cqe{nullptr};
    while (unhandled_cqe_count.load(morder::acquire) > 0) {
        auto attac_guard = attaching_tasks.lock();

        size_t seq_num;

        with(auto compq_guard = completed_cqes.lock()) {
            if (auto ret = ::io_uring_peek_cqe(&this->ring, &cqe); ret != 0 || !cqe) {
                continue;
            }

            seq_num = ::io_uring_cqe_get_data64(cqe);
            (*compq_guard)[seq_num] = *cqe;
            ::io_uring_cqe_seen(&this->ring, cqe);
        }

        sched::task::task_id tid{0};
        if (attac_guard->contains(seq_num)) {
            tid = attac_guard->at(seq_num);
            attac_guard->erase(seq_num);
        }
        if (tid && RT::__worker::task_available(tid)) {
            auto &w = RT::__worker::get_worker_from_task_id(tid);
            w.sc.awake(tid);
            w.awake();
        }

        unhandled_cqe_count.fetch_sub(1, morder::acq_rel);
    }
}

uring::uring(size_t worker_id)
        : worker_id(worker_id)
        , poll_thread{worker_id, ring, completed_cqes, event_fd} {
    ::io_uring_params params{};
    params.sq_entries = io_uring_entries;
    params.cq_entries = io_uring_entries;
    params.flags = IORING_SETUP_SQPOLL | IORING_SETUP_SQ_AFF | IORING_SETUP_CQSIZE;
    params.sq_thread_idle = 100;
    params.sq_thread_cpu = worker_id;

    if (int err = ::io_uring_queue_init_params(io_uring_entries, &this->ring, &params); err < 0) {
        throw runtime_error(
            std::format("[ASCO] uring::uring(): Failed to initialize io_uring: {}", std::strerror(-err)));
    }

    if (auto event_fd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK); event_fd < 0) {
        ::io_uring_queue_exit(&this->ring);
        throw runtime_error(
            std::format("[ASCO] uring::uring(): Failed to create eventfd: {}", std::strerror(errno)));
    } else {
        this->event_fd.store(event_fd, morder::release);
    }

    if (int err = ::io_uring_register_eventfd(&this->ring, this->event_fd.load(morder::acquire)); err < 0) {
        ::close(this->event_fd.load(morder::acquire));
        ::io_uring_queue_exit(&this->ring);
        throw runtime_error(
            std::format(
                "[ASCO] uring::uring(): Failed to register eventfd to io_uring: {}", std::strerror(-err)));
    }

    if (auto numa_node = get_numa_node(worker_id); numa_node >= 0) {
        auto cpumask = get_cpu_set_in_numa_node(numa_node);
        // IO poll kernel thread can be bound to current numa node
        if (int err = ::io_uring_register_iowq_aff(&this->ring, sizeof(cpumask), &cpumask); err < 0) {
            ::io_uring_unregister_eventfd(&this->ring);
            ::close(this->event_fd.load(morder::acquire));
            ::io_uring_queue_exit(&this->ring);
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

bool uring::attach(
    size_t seq_num, sched::task::task_id tid,
    spin<std::unordered_map<size_t, sched::task::task_id>>::guard &&guard) {
    return this->poll_thread.attach(seq_num, tid, std::move(guard));
}

std::optional<int> uring::peek(size_t seq_num) {
    ::io_uring_cqe *cqe{nullptr};
    int res;
    size_t ud;

    if (auto guard = this->completed_cqes.lock(); !guard->empty()) {
        for (auto it = guard->begin(); it != guard->end(); it++) {
            auto &[it_seq_num, it_cqe] = *it;
            if (it_seq_num == seq_num) {
                res = it_cqe.res;
                ud = ::io_uring_cqe_get_data64(&it_cqe);
                guard->erase(it);
                goto finish;
            }
        }
    }

    do {
        cqe = nullptr;
        auto guard = this->completed_cqes.lock();
        if (int ret = ::io_uring_peek_cqe(&this->ring, &cqe); ret == 0 && cqe) {
            if (auto idx = ::io_uring_cqe_get_data64(cqe); idx != seq_num) {
                (*guard)[idx] = *cqe;
            } else {
                res = cqe->res;
                ud = idx;
            }
            poll_thread.decrease_unhandled_cqe_count();
            ::io_uring_cqe_seen(&this->ring, cqe);
        }
    } while (cqe && cqe->user_data != seq_num);

    if (!cqe)
        return std::nullopt;

finish:
    if (auto guard = unpeeked_opens.lock(); guard->contains(ud)) {
        guard->erase(ud);
    }
    this->iotask_count.fetch_sub(1);
    return res;
}

std::expected<int, spin<std::unordered_map<size_t, sched::task::task_id>>::guard>
uring::peek_or_preattach(size_t seq_num) {
    auto guard = poll_thread.prelock_attaching_tasks();
    if (auto res = peek(seq_num); res)
        return std::move(*res);
    else
        return std::unexpected{std::move(guard)};
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

void uring::read_buffer::buffer_destroyer(char *ptr) noexcept { delete reinterpret_cast<read_buffer *>(ptr); }

io::buffer<> uring::read_buffers_iovec::to_buffer(this uring::read_buffers_iovec &&self, size_t size) {
    io::buffer<> res;
    for (size_t i{0}; i < self.nr_vecs; i++) {
        if (!size) {
            uring::read_buffer::buffer_destroyer(reinterpret_cast<char *>(self.vec[i].iov_base));
        } else {
            res.push_raw_array_buffer(
                static_cast<char *>(self.vec[i].iov_base), uring::read_buffer::unit_size,
                std::min(uring::read_buffer::unit_size, size), &uring::read_buffer::buffer_destroyer);
            size -= std::min(uring::read_buffer::unit_size, size);
        }
        self.vec[i].iov_base = nullptr;
        self.vec[i].iov_len = 0;
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
