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

#include "anchordb/env.h"
#include "anchordb/status.h"
#include "env/io_posix.h"
#include "port/port.h" // IWYU pragma: keep
#include "util/thread_pool.h"
#include <bits/types/struct_timeval.h>
#include <cstdint>
#include <dirent.h>
#include <fcntl.h>
#include <liburing.h>
#include <mutex>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace anchordb {

// Common flags defined for all posix open operations
#if defined(HAS_O_CLOEXEC)
constexpr const int kOpenBaseFlags = O_CLOEXEC;
#else
constexpr const int kOpenBaseFlags = 0;
#endif // defined(HAVE_O_CLOEXEC)

// PosixEnv is the POSIX-specific implementation of the environment abstraction.
// It provides file I/O, locking, scheduling, and timing utilities via io_uring
// and standard POSIX APIs.
class PosixEnv : public Env {
  public:
    // Default constructor — creates an environment with default settings.
    PosixEnv();

    // Constructs a PosixEnv with |max_open_files| limiting concurrent open
    // file descriptors, |ring_entries| io_uring submission queue entries,
    // and an optional |direct_io_enabled| flag for O_DIRECT on reads.
    PosixEnv(int max_open_files, uint32_t ring_entries, bool direct_io = false);

    // Destructs the environment, drains the background thread pool, and
    // destroys all io_uring rings (default and thread-local).
    ~PosixEnv() override;

    // Returns a thread-local io_uring ring initialized with |ring_entries_|.
    // Falls back to |default_ring_| if initialization fails.
    struct io_uring *get_thread_local_ring();

    // File creation and I/O

    Status new_sequential_file_reader(const std::string &fname,
                                      SequentialFileReader **result,
                                      size_t buf_size = 0) override;
    Status
    new_random_access_file_reader(const std::string &filename,
                                  RandomAccessFileReader **result) override;
    Status new_writable_file_writer(const std::string &filename,
                                    WritableFileWriter **result,
                                    size_t buf_size = 0) override;
    Status new_appendable_file_writer(const std::string &filename,
                                      WritableFileWriter **result,
                                      size_t buf_size = 0) override;

    bool file_exists(const std::string &filename) override;
    Status get_children(const std::string &dir_path,
                        std::vector<std::string> *result) override;
    Status remove_file(const std::string &filename) override;
    Status create_dir(const std::string &dir_name) override;
    Status remove_dir(const std::string &dir_name) override;
    Status get_file_size(const std::string &filename, uint64_t *size) override;
    Status rename_file(const std::string &from, const std::string &to) override;

    Status lock_file(const std::string &filename, FileLock **lock) override;
    Status unlock_file(FileLock *posix_file_lock) override;

    // Schedules |function| to run on a background thread, passing |arg|.
    void schedule(void (*function)(void *arg), void *arg) override;

    Status get_test_dir(std::string *result) override;

    Status new_logger(const std::string &filename, Logger **result) override;

    // Returns the current time in microseconds since the Unix epoch.
    uint64_t now_micros() override;

    // Sleeps for approximately |micros| microseconds.
    void sleep_for_micros(int micros) override;

  private:
    // Configuration
    const bool direct_io_;
    const uint32_t ring_entries_;

    // File descriptor management
    PosixLockTable locks_;
    Limiter fd_limiter_;

    // io_uring infrastructure
    struct io_uring default_ring_{};
    mutable std::mutex tls_rings_mutex_;
    std::vector<struct io_uring *> registered_tls_rings_;

    // Background thread pool backing schedule(). Sized to the number of
    // hardware threads, with a floor of one worker. Declared last so that it
    // is destroyed first: its workers may touch the io_uring rings above
    // while draining, so those rings must outlive the pool.
    ThreadPool pool{ThreadPool::default_thread_count()};
};

} // namespace anchordb
