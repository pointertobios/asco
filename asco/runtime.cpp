#include <asco/runtime.h>

#include <string>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <pthread.h>

#ifdef __linux__
    #include <sched.h>
    #include <cpuid.h>
    #include <sys/syscall.h>
#endif

#include <asco/utils/while_decl.h>

namespace asco {
    std::map<std::thread::id, worker *> worker::workers;
    std::map<std::thread::id, runtime *> runtime::runtimes;

    worker::worker(int id, const worker_fn &f, task_receiver rx) noexcept
        : id(id)
        , is_calculator(false)
        , task_rx(rx)
        , thread([f, id, this]() {
            f(id, this);
        }) {
        workers[thread.get_id()] = this;
    }

    worker::~worker() {
        join();
    }

    void worker::join() {
        if (!moved)
            thread.join();
    }

    worker::native_handle_type worker::native_handle() {
        return thread.native_handle();
    }

    std::thread::id worker::get_id() const {
        return thread.get_id();
    }

    worker *worker::get_worker() {
        if (workers.find(std::this_thread::get_id()) == workers.end())
            throw std::runtime_error("[ASCO] Currently not in any asco::worker thread");
        return workers[std::this_thread::get_id()];
    }

    /* ---- runtime ---- */

#ifdef __linux__
    static std::vector<cpu_set_t> get_cpus() {
        std::vector<cpu_set_t> cpus;
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        for (int i = 0; i < std::thread::hardware_concurrency(); i++) {
            CPU_SET(i, &cpuset);
            cpus.push_back(cpuset);
        }
        return cpus;
    }
#endif

    runtime::runtime(int nthread_)
            : nthread(nthread_ ? nthread_ : std::thread::hardware_concurrency()) {
        auto worker_lambda = [this](size_t id, worker *self) {

            pthread_setname_np(pthread_self(), std::format("asco::worker{}", id).c_str());
#ifdef __linux__
            self->pid = syscall(SYS_gettid);
#endif
            while (true) if (auto task = self->task_rx->recv(); task.has_value()) {
                try {
                    auto f = task.value();
                    f();
                } catch (const std::exception &e) {
                    std::cerr << e.what() << std::endl;
                }
            } else break;
        };

        pool.reserve(nthread);
#ifdef __linux__
        auto cpus = get_cpus();
        FILE *f;
#endif

        auto [io_tx, io_rx_] = channel<task_sending_instance>(128);
        io_task_tx = std::move(io_tx);
        task_receiver io_rx = make_shared_receiver(std::move(io_rx_));

        auto [calcu_tx, calcu_rx_] = channel<task_sending_instance>(128);
        calcu_task_tx = std::move(calcu_tx);
        task_receiver calcu_rx = make_shared_receiver(std::move(calcu_rx_));

        for (int i = 0; i < nthread; i++) {
            bool is_calculator = false;
#ifdef __linux__
            // The hyper thread cores are usually the high frequency cores
            // Use them as calculator workers
            std::string path = std::format("/sys/devices/system/cpu/cpu{}/topology/thread_siblings_list", i % cpus.size());
            std::ifstream f(path);
            if (!f.is_open())
                throw "[CoIO] Failed to detect CPU hyperthreading";
            std::string buf;
            std::vector<int> siblings;
            while (std::getline(f, buf, '-')) {
                siblings.push_back(std::atoi(buf.c_str()));
            }
            if (siblings.size() > 1) {
                is_calculator = true;
            }
#endif

            if (is_calculator) {
                calcu_worker_count++;
            } else {
                io_worker_count++;
            }

            task_receiver rx;
            if (is_calculator) {
                rx = calcu_rx;
            } else {
                rx = io_rx;
            }
            auto *p = new worker(i, worker_lambda, rx);
            if (!p)
                throw std::runtime_error("[ASCO] Failed to create worker");
            pool.push_back(p);
            auto &workeri = pool[i];
            runtimes[workeri->get_id()] = this;

#ifdef __linux__
            auto &cpu = cpus[i % cpus.size()];
            while (!workeri->pid);
            if (sched_setaffinity(
                workeri->pid,
                sizeof(decltype(cpu)),
                &cpu
            ) == -1) {
                throw std::runtime_error("[ASCO] Failed to set affinity");
            }
            
            workeri->is_calculator = is_calculator;
#endif
        }
    }

    runtime::~runtime() {
        io_task_tx.value().stop();
        calcu_task_tx.value().stop();
        for (auto thread : pool) {
            thread->join();
        }
    }

    runtime *runtime::get_runtime() {
        return runtimes[std::this_thread::get_id()];
    }
};
