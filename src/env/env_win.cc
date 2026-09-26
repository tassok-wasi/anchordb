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

#include "anchordb/env.h"
#include "anchordb/status.h"
#include "env/env_win.h"
#include "env/io_win.h"
#include "util/win_logger.h"

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace anchordb {

WinEnv::WinEnv() : WinEnv(1000, true) {}

WinEnv::WinEnv(int max_open_files, bool direct_io)
    : direct_io_(direct_io), fd_limiter_(max_open_files) {}

WinEnv::~WinEnv() {
    // Signal background worker threads to stop after finishing remaining
    // work
    {
        std::lock_guard<std::mutex> lock(bg_work_mutex_);
        shutting_down_ = true;
        bg_work_cv_.notify_all();
    }

    // Join background worker threads
    for (std::thread &th : bg_threads_) {
        if (th.joinable()) {
            th.join();
        }
    }
}

Status WinEnv::new_sequential_file_reader(const std::string &filename,
                                          SequentialFileReader **result,
                                          size_t buffer_size) {
    DWORD flags_and_attributes = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED;
    if (direct_io_) {
        flags_and_attributes |= FILE_FLAG_NO_BUFFERING;
    }

    HANDLE hFile =
        ::CreateFileA(filename.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                      OPEN_EXISTING, flags_and_attributes, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return win_error(filename, ::GetLastError());
    }

    size_t buf_size =
        (buffer_size != 0) ? buffer_size : k_seq_file_reader_buf_size;
    AlignedBuffer buf(buf_size, k_o_direct_alignment);
    *result = new WinSequentialFileReader(filename, hFile, &buf, buf_size,
                                          direct_io_);

    return Status::ok();
}

Status WinEnv::new_random_access_file_reader(const std::string &filename,
                                             RandomAccessFileReader **result) {
    HANDLE hFile = INVALID_HANDLE_VALUE;

    if (fd_limiter_.acquire()) {
        DWORD flags_and_attributes =
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED;
        if (direct_io_) {
            flags_and_attributes |= FILE_FLAG_NO_BUFFERING;
        }

        hFile = ::CreateFileA(filename.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, flags_and_attributes,
                              nullptr);
        if (hFile == INVALID_HANDLE_VALUE) {
            DWORD err = ::GetLastError();
            // Release the limiter slot if open failed
            fd_limiter_.release();
            return win_error(filename, err);
        }
    }

    *result = new WinRandomAccessFileReader(filename, hFile, &fd_limiter_,
                                            direct_io_);
    return Status::ok();
}

Status WinEnv::new_writable_file_writer(const std::string &filename,
                                        WritableFileWriter **result,
                                        size_t buffer_size) {
    DWORD flags_and_attributes = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED;
    if (direct_io_) {
        flags_and_attributes |= FILE_FLAG_NO_BUFFERING;
    }

    HANDLE hFile =
        ::CreateFileA(filename.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                      CREATE_ALWAYS, flags_and_attributes, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return win_error(filename, ::GetLastError());
    }

    size_t buf_size =
        (buffer_size != 0) ? buffer_size : k_writable_file_buf_size;
    AlignedBuffer buf(buf_size, k_o_direct_alignment);
    *result = new WinWritableFileWriter(filename, hFile, &buf, direct_io_);

    return Status::ok();
}

Status WinEnv::new_appendable_file_writer(const std::string &filename,
                                          WritableFileWriter **result,
                                          size_t buffer_size) {
    DWORD flags_and_attributes = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED;
    if (direct_io_) {
        flags_and_attributes |= FILE_FLAG_NO_BUFFERING;
    }

    HANDLE hFile =
        ::CreateFileA(filename.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                      OPEN_ALWAYS, flags_and_attributes, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return win_error(filename, ::GetLastError());
    }

    // Seek to end of file for append mode.
    LARGE_INTEGER zero = {};
    if (!::SetFilePointerEx(hFile, zero, nullptr, FILE_END)) {
        DWORD err = ::GetLastError();
        ::CloseHandle(hFile);
        return win_error(filename, err);
    }

    size_t buf_size =
        (buffer_size != 0) ? buffer_size : k_writable_file_buf_size;
    AlignedBuffer buf(buf_size, k_o_direct_alignment);

    uint64_t file_size = 0;
    Status s = get_file_size(filename, &file_size);
    if (!s.is_ok()) {
        return s;
    }

    *result =
        new WinWritableFileWriter(filename, hFile, &buf, direct_io_, file_size);

    return Status::ok();
}

bool WinEnv::file_exists(const std::string &filename) {
    DWORD attrs = ::GetFileAttributesA(filename.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES &&
           !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

Status WinEnv::get_children(const std::string &dir_path,
                            std::vector<std::string> *result) {
    result->clear();

    std::string search_path = dir_path + "\\*";
    WIN32_FIND_DATAA find_data;
    HANDLE hFind = ::FindFirstFileA(search_path.c_str(), &find_data);
    if (hFind == INVALID_HANDLE_VALUE) {
        DWORD err = ::GetLastError();
        if (err == ERROR_FILE_NOT_FOUND) {
            return Status::ok(); // Empty directory
        }
        return win_error(dir_path, err);
    }

    do {
        result->emplace_back(find_data.cFileName);
    } while (::FindNextFileA(hFind, &find_data));

    DWORD err = ::GetLastError();
    ::FindClose(hFind);
    if (err != ERROR_NO_MORE_FILES) {
        return win_error(dir_path, err);
    }

    return Status::ok();
}

Status WinEnv::remove_file(const std::string &filename) {
    if (!::DeleteFileA(filename.c_str())) {
        return win_error(filename, ::GetLastError());
    }
    return Status::ok();
}

Status WinEnv::create_dir(const std::string &dir_name) {
    if (!::CreateDirectoryA(dir_name.c_str(), nullptr)) {
        return win_error(dir_name, ::GetLastError());
    }
    return Status::ok();
}

Status WinEnv::remove_dir(const std::string &dir_name) {
    if (!::RemoveDirectoryA(dir_name.c_str())) {
        return win_error(dir_name, ::GetLastError());
    }
    return Status::ok();
}

Status WinEnv::get_file_size(const std::string &filename, uint64_t *size) {
    WIN32_FILE_ATTRIBUTE_DATA file_info;
    if (!::GetFileAttributesExA(filename.c_str(), GetFileExInfoStandard,
                                &file_info)) {
        *size = 0;
        return win_error(filename, ::GetLastError());
    }

    ULARGE_INTEGER file_size;
    file_size.LowPart = file_info.nFileSizeLow;
    file_size.HighPart = file_info.nFileSizeHigh;
    *size = file_size.QuadPart;
    return Status::ok();
}

Status WinEnv::rename_file(const std::string &from, const std::string &to) {
    // MOVEFILE_REPLACE_EXISTING matches POSIX rename() semantics.
    if (!::MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        return win_error(from, ::GetLastError());
    }
    return Status::ok();
}

Status WinEnv::lock_file(const std::string &filename, FileLock **lock) {
    *lock = nullptr;

    HANDLE hFile = ::CreateFileA(filename.c_str(), GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return win_error(filename, ::GetLastError());
    }

    if (!locks_.insert(filename)) {
        ::CloseHandle(hFile);
        return Status::io_error("lock " + filename, "already held by process");
    }

    if (lock_or_unlock(hFile, true) == -1) {
        DWORD lock_err = ::GetLastError();
        ::CloseHandle(hFile);
        locks_.remove(filename);
        return win_error("lock " + filename, lock_err);
    }

    *lock = new WinFileLock(hFile, filename);
    return Status::ok();
}

Status WinEnv::unlock_file(FileLock *lock) {
    WinFileLock *win_file_lock = static_cast<WinFileLock *>(lock);
    if (lock_or_unlock(win_file_lock->hFile(), false) == -1) {
        return win_error("unlock " + win_file_lock->filename(),
                         ::GetLastError());
    }

    locks_.remove(win_file_lock->filename());
    ::CloseHandle(win_file_lock->hFile());
    delete win_file_lock;
    return Status::ok();
}

void WinEnv::schedule(void (*function)(void *arg), void *arg) {
    std::lock_guard<std::mutex> lock(bg_work_mutex_);

    // Lazy-start the dedicated background execution thread on first
    // scheduled job
    if (!bg_threads_started_) {
        bg_threads_started_ = true;
        bg_threads_.emplace_back([this] { bg_thread_main(); });
    }

    // Queue the task
    bg_work_queue_.push(BackgroundTask{function, arg});
    bg_work_cv_.notify_one();
}

void WinEnv::bg_thread_main() {
    while (true) {
        BackgroundTask task;
        {
            std::unique_lock<std::mutex> lock(bg_work_mutex_);
            bg_work_cv_.wait(lock, [this] {
                return shutting_down_ || !bg_work_queue_.empty();
            });

            if (shutting_down_ && bg_work_queue_.empty()) {
                return;
            }

            task = bg_work_queue_.front();
            bg_work_queue_.pop();
        }

        // Execute background callback
        (*task.function)(task.arg);
    }
}

Status WinEnv::get_test_dir(std::string *result) {
    const char *env = std::getenv("TEST_TMPDIR");
    if (env && env[0] != '\0') {
        *result = env;
    } else {
        char buf[MAX_PATH];
        DWORD len = ::GetTempPathA(sizeof(buf), buf);
        if (len == 0 || len > sizeof(buf)) {
            return Status::io_error("Failed to get temp path");
        }
        std::snprintf(buf + len, sizeof(buf) - len, "anchordbtest-%lu",
                      ::GetCurrentProcessId());
        *result = buf;
    }

    // The CreateDir status is ignored because the directory may already
    // exist.
    create_dir(*result);

    return Status::ok();
}

Status WinEnv::new_logger(const std::string &filename, Logger **result) {
    std::FILE *fp = std::fopen(filename.c_str(), "a");
    if (fp == nullptr) {
        *result = nullptr;
        return Status::io_error(filename, "Failed to open log file");
    } else {
        *result = new WinLogger(fp);
        return Status::ok();
    }
}

uint64_t WinEnv::now_micros() {
    // GetSystemTimePreciseAsFileTime gives 100ns ticks since Jan 1, 1601.
    // Convert to microseconds since Unix epoch (Jan 1, 1970).
    FILETIME ft;
    ::GetSystemTimePreciseAsFileTime(&ft);

    ULARGE_INTEGER ticks;
    ticks.LowPart = ft.dwLowDateTime;
    ticks.HighPart = ft.dwHighDateTime;

    // 100ns ticks -> microseconds: divide by 10.
    // Subtract the offset between 1601 and 1970 in 100ns units:
    // 116444736000000000 = number of 100ns intervals from 1601 to 1970.
    constexpr uint64_t kEpochOffset = 116444736000000000ULL;
    return (ticks.QuadPart - kEpochOffset) / 10;
}

void WinEnv::sleep_for_micros(int micros) {
    std::this_thread::sleep_for(std::chrono::microseconds(micros));
}

} // namespace anchordb
