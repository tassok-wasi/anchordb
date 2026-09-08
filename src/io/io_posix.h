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
#include "anchordb/slice.h"
#include "anchordb/status.h"
#include "io/aligned_buf.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <liburing.h>
#include <mutex>
#include <set>
#include <string>
#include <unistd.h>

namespace anchordb {
// Size of the internal write buffer used by PosixWritableFile.
constexpr const size_t k_writable_file_buffer_size = 65536;
constexpr const size_t k_direct_io_alignment = 4096;

// Constructs a Status from a POSIX error number, incorporating |context|
// into the error message for diagnostic purposes.
Status posix_error(const std::string &context, int error_number);

// Limiter is a simple concurrency limiter that allows up to |max_acquires|
// simultaneous acquisitions. It is used to bound the number of open file
// descriptors cached by the system.
//
// Thread-safe.
class Limiter {
  public:
    // Constructs a Limiter that allows at most |max_acquires| concurrent
    // acquisitions. |max_acquires| must be non-negative.
    explicit Limiter(int max_acquires) : allowed_(max_acquires) {}

    Limiter(const Limiter &) = delete;
    Limiter operator=(const Limiter &) = delete;

    // Attempts to acquire a resource. If a slot is available, decrements
    // the available count and returns true. Otherwise, increments the
    // count of waiters (negative available count) and returns false.
    //
    // Callers that receive false should retry later; callers that receive
    // true must eventually call release().
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

    // Releases a resource previously obtained via a successful acquire().
    // Increments the available count so that another caller may acquire.
    void release() noexcept {
        allowed_.fetch_add(1, std::memory_order_relaxed);
    }

  private:
    // Tracks the number of available slots. A positive value indicates
    // free slots; a non-positive value indicates the number of waiters
    // (i.e., callers that called acquire() and received false).
    std::atomic<int> allowed_;
};

// PosixSequentialReadFile provides sequential read access to a file identified
// by a file descriptor. It is not thread-safe and does not support concurrent
// reads. The file descriptor is owned by this object and will be closed upon
// destruction.
class PosixSequentialReadFile final : public SequentialReadFile {
  public:
    // Constructs a PosixSequentialReadFile that takes ownership of |fd|.
    // |filename| is kept for error reporting purposes.
    PosixSequentialReadFile(std::string filename, int fd, struct io_uring *ring,
                            AlignedBuffer **buf = nullptr,
                            bool direct_io = true)
        : fd_(fd), filename_(std::move(filename)), ring_(ring), offset_(0),
          direct_io_(direct_io), staging_buf_(nullptr) {
        if (buf != nullptr && *buf != nullptr) {
            staging_buf_ = std::move(*buf);
            delete buf;
        } else if (direct_io_) {
            // Allocate default 64 KiB sector-aligned buffer for direct I/O
            // staging
            staging_buf_ = new DefaultAlignedBuffer(k_writable_file_buffer_size,
                                                    k_direct_io_alignment);
        }
    }

    // Closes the underlying file descriptor.
    ~PosixSequentialReadFile() override {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    // Reads up to |n| bytes from the file into |scratch|. Sets |*result| to
    // the data that was actually read. Returns OK on success, or a non-OK
    // Status if an error occurs (including partial reads).
    Status read(size_t n, Slice *result, char *scratch) override;

    // Skips |n| bytes forward in the file. Returns OK on success, or a
    // non-OK Status if the skip cannot be completed (e.g., seek past EOF).
    Status skip(uint64_t n) override;

  private:
    Status submit_read_(void *dst, size_t len, uint64_t offset,
                        size_t *bytes_read);

    const int fd_;
    const std::string filename_;
    struct io_uring *const ring_;
    uint64_t offset_;
    const bool direct_io_;

    AlignedBuffer *staging_buf_;
};

// PosixRandomAccessFile provides random read access to a file identified by
// a file descriptor. It supports two modes of operation:
//   - Permanent FD mode (has_permanent_fd_ == true): The file descriptor is
//     owned by this object and closed on destruction. The global FD cache
//     limiter is released at that time.
//   - Temporary FD mode (has_permanent_fd_ == false): The file descriptor is
//     borrowed and must not be closed by this object.
class PosixRandomAccessFile final : public RandomAccessFile {
  public:
    // Constructs a PosixRandomAccessFile.
    // |filename| is kept for error reporting.
    // |fd| is the file descriptor; if >= 0, this object takes ownership.
    // |limiter| is an optional global FD cache limiter; released on destruction
    //          when has_permanent_fd_ is true.
    // |ring| is a io_uring instance for async reads.
    PosixRandomAccessFile(std::string filename, int fd, Limiter *limiter,
                          struct io_uring *ring, bool direct_io = true)
        : filename_(std::move(filename)), has_permanent_fd_(fd >= 0), fd_(fd),
          limiter_(limiter), ring_(ring), direct_io_(direct_io) {}

    // Closes the underlying file descriptor if owned, and releases the limiter
    // resource if applicable.
    ~PosixRandomAccessFile() {
        if (has_permanent_fd_) {
            ::close(fd_);
            if (limiter_ != nullptr) {
                limiter_->release();
            }
        }
    }

    // Reads up to |n| bytes starting at |offset| into |scratch|.
    // Sets |*result| to the data actually read. Returns OK on success,
    // or a non-OK Status on failure (including short reads).
    Status read(uint64_t offset, size_t n, Slice *result,
                char *scratch) override;

  private:
    Status submit_read_(int fd, void *buf, size_t n, uint64_t offset,
                        size_t *bytes_read);

    const std::string filename_;  // File name for diagnostic messages.
    const bool has_permanent_fd_; // True if this object owns |fd_|.
    const int fd_;                // Valid only when has_permanent_fd_ == true.
    Limiter *const limiter_; // Non-owning pointer to global FD cache limiter.
    struct io_uring *const ring_; // Non-owning pointer to global/env io_uring.
    const bool direct_io_; // True if direct I/O is enabled for this file.
};

// PosixWritableFile provides buffered sequential write access to a file
// identified by a file descriptor. It buffers writes in an internal buffer
// and flushes to the underlying file descriptor when the buffer is full or
// when explicitly requested. It also supports syncing the file data to disk
// and, for MANIFEST files, syncing the parent directory to ensure durability.
class PosixWritableFile final : public WritableFile {
  public:
    // Constructs a PosixWritableFile.
    // |filename| is kept for error reporting and to determine if this is a
    // MANIFEST file (which requires an additional directory fsync on sync())..
    // |fd| is the target open file descriptor.
    // |env| is a non-owning pointer to the parent PosixEnv.
    // |buf| is an optional caller-supplied or factory-allocated AlignedBuffer
    // pointer. |direct_io| enables O_DIRECT operational rules.

    PosixWritableFile(std::string filename, int fd, struct io_uring *ring,
                      AlignedBuffer **buf = nullptr, bool direct_io = true)
        : filename_(std::move(filename)), fd_(fd), ring_(ring), filesize_(0),
          direct_io_(direct_io), buf_(nullptr), buf_used_(0),
          is_manifest_(is_manifest(filename_)), dirname_(dirname(filename_)) {
        if (buf != nullptr && *buf != nullptr) {
            buf_ = std::move(*buf);
            delete buf;
        } else {
            // Allocate concrete default aligned buffer if none was provided
            buf_ = new DefaultAlignedBuffer(k_writable_file_buffer_size,
                                            k_direct_io_alignment);
        }
    }

    ~PosixWritableFile() override {
        if (fd_ >= 0) {
            close();
        }
    }

    // Appends |data| to the file. Data is buffered in memory and may not be
    // written to disk until the buffer is full or flush()/sync()/close() is
    // called. Returns OK on success, or a non-OK Status on failure.
    Status append(const Slice &data) override;

    // Flushes any buffered data to the underlying file descriptor. Does not
    // guarantee the data is persisted to disk (see sync()). Returns OK on
    // success, or a non-OK Status on failure.
    Status flush() override { return flush_buffer(); }

    // Closes the file, flushing any buffered data before closing the file
    // descriptor. Returns OK on success, or a non-OK Status on failure.
    Status close() override;

    // Flushes buffered data and calls fsync on the file descriptor. For
    // MANIFEST files, also fsyncs the parent directory to ensure the directory
    // entry is durable. Returns OK on success, or a non-OK Status on failure.
    Status sync() override;

  private:
    Status submit_write_(const char *src, size_t len, uint64_t offset);

    // Writes the contents of the internal buffer to the file descriptor and
    // resets the buffer position. Returns OK on success, or a non-OK Status
    // on failure.
    Status flush_buffer();

    // If this file is a MANIFEST file, fsyncs the parent directory to ensure
    // the directory entry is durable after a rename or creation.
    Status sync_dir_if_manifest();

    // Calls fsync on the given file descriptor |fd|. |fd_path| is used for
    // error reporting. Returns OK on success, or a non-OK Status on failure.
    Status sync_fd(int fd, const std::string &fd_path);

    // Returns the directory component of |filename| (everything before the
    // final '/').
    std::string dirname(const std::string &filename);

    // Returns the final component of |filename| (everything after the final
    // '/').
    Slice basename(const std::string &filename);

    // Returns true if |filename| represents a MANIFEST file (i.e., its
    // basename starts with "MANIFEST").
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
    AlignedBuffer *buf_;
    size_t buf_used_;
};

// Locks or unlocks the file referenced by |fd|. If |lock| is true, an
// exclusive (write) lock is acquired; otherwise, any existing lock is
// released. Returns 0 on success, or a non-zero error code on failure.
int lock_or_unlock(int fd, bool lock);

// PosixFileLock associates a locked file descriptor with its filename.
// Instances are typically created and managed by PosixLockTable.
class PosixFileLock : public FileLock {
  public:
    // Constructs a PosixFileLock for the given |fd| and |filename|.
    PosixFileLock(int fd, std::string filename)
        : fd_(fd), filename_(std::move(filename)) {}

    // Returns the file descriptor associated with this lock.
    int fd() const { return fd_; }

    // Returns the filename associated with this lock.
    const std::string &filename() const { return filename_; }

  private:
    const int fd_;               // Locked file descriptor.
    const std::string filename_; // Name of the locked file.
};

// PosixLockTable maintains an in-process set of currently locked files.
// It prevents the same file from being locked more than once within the
// same process. Thread-safe.
class PosixLockTable {
  public:
    // Attempts to insert |fname| into the set of locked files. Returns true
    // if the file was not already locked, false if it was already present.
    bool insert(const std::string &fname) __attribute__((locks_excluded(mu_))) {
        mu_.lock();
        bool succeeded = locked_files_.insert(fname).second;
        mu_.unlock();
        return succeeded;
    }

    // Removes |fname| from the set of locked files, releasing the lock.
    void remove(const std::string &fname) __attribute__((locks_excluded(mu_))) {
        mu_.lock();
        locked_files_.erase(fname);
        mu_.unlock();
    }

  private:
    std::mutex mu_; // Protects |locked_files_|.
    std::set<std::string> locked_files_ __attribute__((guarded_by(mu_)));
};

} // namespace anchordb
