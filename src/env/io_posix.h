// Copyright 2026 Tassok Imam Wasi
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
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
#include <fcntl.h>
#include <liburing.h>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <unistd.h>

namespace anchordb {

constexpr const size_t k_writable_file_buf_size = 65536;           // 64KB
constexpr const size_t k_seq_file_reader_buf_size = 4194304;       // 4MB
constexpr const size_t k_seq_blob_file_reader_buf_size = 67108864; // 64MB
constexpr const size_t k_o_direct_alignment = 4096;

Status posix_error(const std::string &context, int error_number);

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

// Sequential read via io_uring. Not thread-safe.
class PosixSequentialFileReader final : public SequentialFileReader {
  public:
    PosixSequentialFileReader(std::string filename, int fd,
                              struct io_uring *ring,
                              AlignedBuffer *buf = nullptr,
                              size_t buffer_size = 0, bool direct_io = false)
        : fd_(fd), filename_(std::move(filename)), ring_(ring), offset_(0),
          direct_io_(direct_io), buf_file_offset_(0), buf_bytes_valid_(0) {
        if (buf != nullptr) {
            buf_ = std::move(*buf);
        } else if (direct_io_) {
            size_t buf_size =
                (buffer_size != 0) ? buffer_size : k_seq_file_reader_buf_size;
            buf_ = AlignedBuffer(buf_size, k_o_direct_alignment);
        }
    }

    ~PosixSequentialFileReader() override {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    Status read(size_t n, std::string_view *result, char *scratch) override;
    Status skip(uint64_t n) override;

  private:
    Status submit_read_(void *dst, size_t len, uint64_t offset,
                        size_t *bytes_read);

    const int fd_;
    const std::string filename_;
    struct io_uring *const ring_;
    uint64_t offset_;
    const bool direct_io_;

    AlignedBuffer buf_;
    uint64_t buf_file_offset_;
    size_t buf_bytes_valid_;
};

// Random read via io_uring. Supports permanent or borrowed fd.
class PosixRandomAccessFileReader final : public RandomAccessFileReader {
  public:
    PosixRandomAccessFileReader(std::string filename, int fd, Limiter *limiter,
                                struct io_uring *ring, bool direct_io = true)
        : filename_(std::move(filename)), has_permanent_fd_(fd >= 0), fd_(fd),
          limiter_(limiter), ring_(ring), direct_io_(direct_io) {}

    ~PosixRandomAccessFileReader() override {
        if (has_permanent_fd_) {
            ::close(fd_);
            if (limiter_ != nullptr) {
                limiter_->release();
            }
        }
    }

    Status read(uint64_t offset, size_t n, std::string_view *result,
                char *scratch) override;

  private:
    Status submit_read_(int fd, void *buf, size_t n, uint64_t offset,
                        size_t *bytes_read);

    const std::string filename_;
    const bool has_permanent_fd_;
    const int fd_;
    Limiter *const limiter_;
    struct io_uring *const ring_;
    const bool direct_io_;
};

// Buffered sequential write via io_uring. For MANIFEST files, sync() also
// fsyncs the parent directory.
class PosixWritableFileWriter final : public WritableFileWriter {
  public:
    PosixWritableFileWriter(std::string filename, int fd, struct io_uring *ring,
                            AlignedBuffer *buf = nullptr, bool direct_io = true,
                            uint64_t initial_filesize = 0)
        : filename_(std::move(filename)), fd_(fd), ring_(ring),
          filesize_(initial_filesize), direct_io_(direct_io),
          is_manifest_(is_manifest(filename_)), dirname_(dirname(filename_)) {
        if (buf != nullptr) {
            buf_ = std::move(*buf);
        } else {
            buf_ =
                AlignedBuffer(k_writable_file_buf_size, k_o_direct_alignment);
        }
    }

    ~PosixWritableFileWriter() override {
        if (fd_ >= 0) {
            close();
        }
    }

    Status append(std::string_view data) override;
    Status flush() override { return flush_buffer(); }
    Status close() override;
    Status sync() override;

  private:
    Status submit_write_(const char *src, size_t len, uint64_t offset);
    Status flush_buffer();
    Status sync_dir_if_manifest();
    Status sync_fd(int fd, const std::string &fd_path);

    std::string dirname(const std::string &filename);
    std::string_view basename(const std::string &filename);

    bool is_manifest(const std::string &filename) {
        return basename(filename).starts_with("MANIFEST");
    }

    const std::string filename_;
    const bool is_manifest_;
    const std::string dirname_;
    int fd_;
    struct io_uring *const ring_;
    uint64_t filesize_;
    const bool direct_io_;
    AlignedBuffer buf_;
};

// Acquires or releases an exclusive file lock on |fd|.
int lock_or_unlock(int fd, bool lock);

class PosixFileLock : public FileLock {
  public:
    PosixFileLock(int fd, std::string filename)
        : fd_(fd), filename_(std::move(filename)) {}

    int fd() const { return fd_; }
    const std::string &filename() const { return filename_; }

  private:
    const int fd_;
    const std::string filename_;
};

// In-process set of locked files. Prevents double-locking. Thread-safe.
class PosixLockTable {
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
