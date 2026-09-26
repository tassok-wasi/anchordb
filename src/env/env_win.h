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
#include "env/io_win.h"
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace anchordb {

// WinEnv is the Windows-specific implementation of the environment abstraction.
// It provides file I/O, locking, scheduling, and timing utilities via IOCP
// (overlapped I/O) and standard Win32 APIs.
class WinEnv : public Env {
  public:
    // Default constructor — creates an environment with default settings.
    WinEnv();

    // Constructs a WinEnv with |max_open_files| limiting concurrent open
    // file handles and an optional |direct_io_enabled| flag for
    // FILE_FLAG_NO_BUFFERING on reads.
    WinEnv(int max_open_files, bool direct_io = false);

    // Destructs the environment, joins background threads.
    ~WinEnv() override;

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
    Status unlock_file(FileLock *win_file_lock) override;

    // Schedules |function| to run on a background thread, passing |arg|.
    void schedule(void (*function)(void *arg), void *arg) override;

    Status get_test_dir(std::string *result) override;

    Status new_logger(const std::string &filename, Logger **result) override;

    // Returns the current time in microseconds since the Unix epoch.
    uint64_t now_micros() override;

    // Sleeps for approximately |micros| microseconds.
    void sleep_for_micros(int micros) override;

  private:
    // Represents a pending background task.
    struct BackgroundTask {
        void (*function)(void *arg);
        void *arg;
    };

    // Main loop for each background worker thread.
    void bg_thread_main();

    // Configuration
    const bool direct_io_;

    // File handle management
    WinLockTable locks_;
    Limiter fd_limiter_;

    // Background thread pool
    std::mutex bg_work_mutex_;
    std::condition_variable bg_work_cv_;
    std::queue<BackgroundTask> bg_work_queue_;
    std::vector<std::thread> bg_threads_;
    bool bg_threads_started_{false};
    bool shutting_down_{false};
};

} // namespace anchordb
