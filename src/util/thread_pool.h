// Copyright 2026 Tassok Imam Wasi

// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0

// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "port/port_thread.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

// A fixed-size pool of worker threads that execute queued tasks.
//
// Tasks are executed outside the queue lock so that workers can run in
// parallel. On destruction the pool performs a graceful shutdown: workers
// finish the tasks already queued (new tasks are rejected) and are then
// joined.
//
// Thread creation is best-effort: if the system cannot start all requested
// workers, the pool runs with however many it managed to start. If none could
// be started, the pool rejects every task (enqueue returns false). Use
// thread_count() to observe the actual number of workers.
class ThreadPool {
  public:
    using Task = std::function<void()>;

    // Returns a sensible default worker count: the number of hardware threads,
    // falling back to 1 when that cannot be determined.
    static size_t default_thread_count() noexcept {
        const unsigned int hw = std::thread::hardware_concurrency();
        return hw == 0 ? 1 : static_cast<size_t>(hw);
    }

    // Constructs a pool with `thread_count` worker threads. A count of zero
    // falls back to default_thread_count().
    explicit ThreadPool(size_t thread_count = default_thread_count())
        : stop_(false), active_workers_(0) {
        if (thread_count == 0) { thread_count = default_thread_count(); }

        workers_.reserve(thread_count);
        for (size_t i = 0; i < thread_count; ++i) {
            NativeThreadHandle handle{};
            if (!thread_create(handle, &ThreadPool::worker_entry, this)) {
                break; // Best-effort: keep the workers we managed to start.
            }
            workers_.push_back(handle);
        }

        if (workers_.empty()) {
            // No worker could be started. Reject tasks rather than queue work
            // that will never run.
            stop_ = true;
        }
    }

    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;
    ThreadPool(ThreadPool &&) = delete;
    ThreadPool &operator=(ThreadPool &&) = delete;

    ~ThreadPool() { shutdown(); }

    // Stops accepting new tasks, lets the workers finish the tasks already
    // queued, and joins them. Idempotent, so it is safe to call explicitly
    // before destruction when the pool must be drained ahead of other members
    // (see PosixEnv::~PosixEnv).
    void shutdown() noexcept {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (stop_) {
                return; // Already shut down or never started.
            }
            stop_ = true;
        }
        cv_.notify_all();
        join_workers();
    }

    // Schedules a std::function<void()> task into the queue.
    // Returns false if the pool is shutting down and the task was rejected.
    bool enqueue(Task task) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (stop_) {
                return false; // Reject tasks if pool is shutting down
            }
            tasks_.push(std::move(task));
        }
        cv_.notify_one();
        return true;
    }

    // C-style function pointer overload.
    // Returns false if `function` is null or the pool is shutting down.
    bool enqueue(void (*function)(void *), void *arg) {
        if (function == nullptr) { return false; }
        return enqueue([function, arg]() { function(arg); });
    }

    // Returns the total number of worker threads in the pool.
    size_t thread_count() const noexcept { return workers_.size(); }

    // Lock-free check for the number of tasks currently executing.
    size_t active_workers() const noexcept {
        return active_workers_.load(std::memory_order_relaxed);
    }

    // Returns true when no tasks are queued and none are executing.
    bool is_idle() const {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return tasks_.empty() &&
               active_workers_.load(std::memory_order_relaxed) == 0;
    }

  private:
#if defined(_WIN32)
    static unsigned __stdcall worker_entry(void *arg) {
        static_cast<ThreadPool *>(arg)->worker_loop();
        return 0;
    }
#else
    static void *worker_entry(void *arg) {
        static_cast<ThreadPool *>(arg)->worker_loop();
        return nullptr;
    }
#endif

    void worker_loop() {
        while (true) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });

                if (stop_ && tasks_.empty()) { return; }

                task = std::move(tasks_.front());
                tasks_.pop();

                // Count the task as active while still holding the lock so that
                // is_idle() cannot observe an empty queue with no active task
                // while this task is in flight.
                active_workers_.fetch_add(1, std::memory_order_relaxed);
            }

            // Execute the task outside the lock to allow parallel
            // ingestion/execution.
            task();
            active_workers_.fetch_sub(1, std::memory_order_relaxed);
        }
    }

    void join_workers() noexcept {
        for (NativeThreadHandle handle : workers_) { thread_join(handle); }
    }

    std::vector<NativeThreadHandle> workers_;
    std::queue<Task> tasks_;
    mutable std::mutex queue_mutex_;
    std::condition_variable cv_;
    bool stop_;
    std::atomic<size_t> active_workers_;
};

} // namespace ANCHORDB_NAMESPACE
