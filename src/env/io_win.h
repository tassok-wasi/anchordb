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
#include "util/aligned_buffer.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

constexpr const size_t k_writable_file_buf_size = 65536;           // 64KB
constexpr const size_t k_seq_file_reader_buf_size = 4194304;       // 4MB
constexpr const size_t k_seq_blob_file_reader_buf_size = 67108864; // 64MB
constexpr const size_t k_o_direct_alignment = 4096;

Status win_error(const std::string &context, DWORD error_number);

// Concurrency limiter bounding the number of open file descriptors.
// Thread-safe.
class Limiter {
  public:
    explicit Limiter(int max_acquires) : allowed_(max_acquires) {}

    Limiter(const Limiter &) = delete;
    Limiter operator=(const Limiter &) = delete;

    // Returns true if a slot was acquired; caller must call release().
    bool acquire() noexcept {
        if (allowed_.load(std::memory_order_relaxed) <= 0)
            return false;
        int current = allowed_.fetch_sub(1, std::memory_order_relaxed);
        if (current <= 0) {
            allowed_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        return true;
    }

    void release() noexcept {
        allowed_.fetch_add(1, std::memory_order_relaxed);
    }

  private:
    // Positive = free slots; non-positive = number of waiters.
    std::atomic<int> allowed_;
};

// Sequential read via IOCP. Not thread-safe.
class WinSequentialFileReader final : public SequentialFileReader {
  public:
    WinSequentialFileReader(std::string filename, HANDLE hFile,
                            AlignedBuffer *buf = nullptr,
                            size_t buffer_size = 0, bool direct_io = false)
        : hFile_(hFile), filename_(std::move(filename)), offset_(0),
          direct_io_(direct_io), buf_file_offset_(0), buf_bytes_valid_(0) {
        if (buf != nullptr) {
            buf_ = std::move(*buf);
        } else if (direct_io_) {
            size_t buf_size =
                (buffer_size != 0) ? buffer_size : k_seq_file_reader_buf_size;
            buf_ = AlignedBuffer(buf_size, k_o_direct_alignment);
        }
    }

    ~WinSequentialFileReader() override {
        if (hFile_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(hFile_);
        }
    }

    Status read(size_t n, std::string_view *result, char *scratch) override;
    Status skip(uint64_t n) override;

  private:
    Status submit_read_(void *dst, size_t len, uint64_t offset,
                        size_t *bytes_read);

    const HANDLE hFile_;
    const std::string filename_;
    uint64_t offset_;
    const bool direct_io_;

    AlignedBuffer buf_;
    uint64_t buf_file_offset_;
    size_t buf_bytes_valid_;
};

// Random read via IOCP. Supports permanent or borrowed handle.
class WinRandomAccessFileReader final : public RandomAccessFileReader {
  public:
    WinRandomAccessFileReader(std::string filename, HANDLE hFile,
                              Limiter *limiter, bool direct_io = true)
        : filename_(std::move(filename)),
          has_permanent_handle_(hFile != INVALID_HANDLE_VALUE), hFile_(hFile),
          limiter_(limiter), direct_io_(direct_io) {}

    ~WinRandomAccessFileReader() override {
        if (has_permanent_handle_) {
            ::CloseHandle(hFile_);
            if (limiter_ != nullptr) {
                limiter_->release();
            }
        }
    }

    Status read(uint64_t offset, size_t n, std::string_view *result,
                char *scratch) override;

  private:
    Status submit_read_(HANDLE hFile, void *buf, size_t n, uint64_t offset,
                        size_t *bytes_read);

    const std::string filename_;
    const bool has_permanent_handle_;
    const HANDLE hFile_;
    Limiter *const limiter_;
    const bool direct_io_;
};

// Buffered sequential write via IOCP. For MANIFEST files, sync() also
// fsyncs the parent directory.
class WinWritableFileWriter final : public WritableFileWriter {
  public:
    WinWritableFileWriter(std::string filename, HANDLE hFile,
                          AlignedBuffer *buf = nullptr, bool direct_io = true,
                          uint64_t initial_filesize = 0)
        : filename_(std::move(filename)), hFile_(hFile),
          filesize_(initial_filesize), direct_io_(direct_io),
          is_manifest_(is_manifest(filename_)), dirname_(dirname(filename_)) {
        if (buf != nullptr) {
            buf_ = std::move(*buf);
        } else {
            buf_ =
                AlignedBuffer(k_writable_file_buf_size, k_o_direct_alignment);
        }
    }

    ~WinWritableFileWriter() override {
        if (hFile_ != INVALID_HANDLE_VALUE) {
            close();
        }
    }

    Status append(const std::string_view &data) override;
    Status flush() override { return flush_buffer(); }
    Status close() override;
    Status sync() override;

  private:
    Status submit_write_(const char *src, size_t len, uint64_t offset);
    Status flush_buffer();
    Status sync_dir_if_manifest();
    Status sync_handle(HANDLE hFile, const std::string &file_path);

    std::string dirname(const std::string &filename);
    std::string_view basename(const std::string &filename);

    bool is_manifest(const std::string &filename) {
        return basename(filename).starts_with("MANIFEST");
    }

    const std::string filename_;
    const bool is_manifest_;
    const std::string dirname_;
    HANDLE hFile_;
    uint64_t filesize_;
    const bool direct_io_;
    AlignedBuffer buf_;
};

// Acquires or releases an exclusive file lock on |hFile|.
int lock_or_unlock(HANDLE hFile, bool lock);

class WinFileLock : public FileLock {
  public:
    WinFileLock(HANDLE hFile, std::string filename)
        : hFile_(hFile), filename_(std::move(filename)) {}

    HANDLE hFile() const { return hFile_; }
    const std::string &filename() const { return filename_; }

  private:
    const HANDLE hFile_;
    const std::string filename_;
};

// In-process set of locked files. Prevents double-locking. Thread-safe.
class WinLockTable {
  public:
    bool insert(const std::string &fname) {
        mu_.lock();
        bool succeeded = locked_files_.insert(fname).second;
        mu_.unlock();
        return succeeded;
    }

    void remove(const std::string &fname) {
        mu_.lock();
        locked_files_.erase(fname);
        mu_.unlock();
    }

  private:
    std::mutex mu_;
    std::set<std::string> locked_files_;
};

} // namespace anchordb
