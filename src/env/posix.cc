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
#include "env/env_default.h"
#include "env/posix.h"
#include "file/random_access_reader.h"
#include "file/sequential_reader.h"
#include "file/writer.h"
#include "util/posix_logger.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>

#include "anchordb/anchordb_namespace.h"

#ifndef ANCHORDB_DIRECT_IO_DEFAULT
    #define ANCHORDB_DIRECT_IO_DEFAULT 1
#endif

namespace ANCHORDB_NAMESPACE {

namespace {

std::atomic<uint64_t> g_next_env_id{1};

uint64_t next_env_id() {
    return g_next_env_id.fetch_add(1, std::memory_order_relaxed);
}

#if HAS_IO_URING
// A thread-local io_uring ring owned by the thread that created it. Keying by
// env id prevents registries from different PosixEnv instances (or a later
// instance reusing the same address) from colliding, and the ring is freed
// when the owning thread exits rather than by the environment.
struct TlsRingEntry {
    uint64_t env_id;
    std::unique_ptr<struct io_uring> ring;
};
#endif

} // namespace

namespace env_internal {

Env *create_default_env() { return new PosixEnv(); }

} // namespace env_internal

PosixEnv::PosixEnv() : PosixEnv(1000, 256, ANCHORDB_DIRECT_IO_DEFAULT != 0) {}

PosixEnv::PosixEnv(int max_open_files, uint32_t ring_entries, bool direct_io)
    : direct_io_(direct_io), ring_entries_(ring_entries),
      env_id_(next_env_id()), fd_limiter_(max_open_files) {
    // Failure is not fatal: get_thread_local_ring() reports nullptr and the
    // I/O paths fall back to synchronous pread/pwrite. Construction must not
    // throw; the outcome is reported through ring_status().
#if HAS_IO_URING
    default_ring_ = new struct io_uring();
    int ret = io_uring_queue_init(ring_entries, default_ring_, 0);
    if (ret < 0) {
        delete default_ring_;
        default_ring_ = nullptr;
        default_ring_valid_ = false;
        ring_status_ = Status::io_error(std::string_view("io_uring_queue_init"),
                                        std::strerror(-ret));
    } else {
        default_ring_valid_ = true;
        ring_status_ = Status::ok();
    }
#else
    default_ring_valid_ = false;
    ring_status_ = Status::not_supported(
        std::string_view("io_uring support not compiled in"));
#endif
}

Status PosixEnv::create(int max_open_files, uint32_t ring_entries,
                        bool direct_io, PosixEnv **result) {
    if (result == nullptr) {
        return Status::invalid_argument(
            std::string_view("PosixEnv::create: result is null"));
    }
    PosixEnv *env = new PosixEnv(max_open_files, ring_entries, direct_io);
    *result = env;
    return env->ring_status();
}

PosixEnv::~PosixEnv() {
    // Drain the background pool before tearing down the io_uring rings. The
    // pool's destructor rejects new work, finishes what is already queued,
    // and joins its workers, so no scheduled task can touch a ring after this
    // point. Each worker's thread-local ring is freed when that worker exits
    // during the join.
    pool.shutdown();

#if HAS_IO_URING
    // Destroy the shared default ring, but only if it was initialized.
    if (default_ring_valid_ && default_ring_ != nullptr) {
        io_uring_queue_exit(default_ring_);
    }
    delete default_ring_;
    default_ring_ = nullptr;
#endif
}

struct io_uring *PosixEnv::get_thread_local_ring() {
#if !HAS_IO_URING
    return nullptr;
#else
    thread_local std::vector<TlsRingEntry> tls_rings;

    for (const TlsRingEntry &entry : tls_rings) {
        if (entry.env_id == env_id_) { return entry.ring.get(); }
    }

    auto ring = std::make_unique<struct io_uring>();
    if (io_uring_queue_init(ring_entries_, ring.get(), 0) < 0) {
        return default_ring_valid_ ? default_ring_ : nullptr;
    }

    struct io_uring *raw_ring = ring.get();
    tls_rings.push_back(TlsRingEntry{env_id_, std::move(ring)});
    return raw_ring;
#endif
}

Status PosixEnv::new_sequential_file_reader(const std::string &filename,
                                            SequentialFileReader **result,
                                            size_t buffer_size) {
    struct io_uring *ring = get_thread_local_ring();

    int flags = O_RDONLY | kOpenBaseFlags;
    if (direct_io_) { flags |= O_DIRECT; }

    int fd = ::open(filename.c_str(), flags, 0);
    if (fd < 0) { return posix_error(filename, errno); }

    size_t buf_size =
        (buffer_size != 0) ? buffer_size : k_seq_file_reader_buf_size;
    AlignedBuffer buf(buf_size, k_o_direct_alignment);
    *result = new PosixSequentialFileReader(filename, fd, ring, &buf, buf_size,
                                            direct_io_);

    return Status::ok();
}

Status
PosixEnv::new_random_access_file_reader(const std::string &filename,
                                        RandomAccessFileReader **result) {
    int fd = -1;

    // Calling thread's io_uring ring (nullptr when unavailable).
    struct io_uring *ring = get_thread_local_ring();

    if (fd_limiter_.acquire()) {
        int flags = O_RDONLY | kOpenBaseFlags;
        if (direct_io_) { flags |= O_DIRECT; }

        fd = ::open(filename.c_str(), flags, 0644);
        if (fd < 0) {
            int err = errno;
            // Release the limiter slot if open failed
            fd_limiter_.release();
            return posix_error(filename, err);
        }
    }

    *result = new PosixRandomAccessFileReader(filename, fd, &fd_limiter_, ring,
                                              direct_io_);
    return Status::ok();
}

Status PosixEnv::new_writable_file_writer(const std::string &filename,
                                          WritableFileWriter **result,
                                          size_t buffer_size) {
    // Calling thread's io_uring ring (nullptr when unavailable).
    struct io_uring *ring = get_thread_local_ring();

    int flags = O_CREAT | O_WRONLY | O_TRUNC | kOpenBaseFlags;
    if (direct_io_) { flags |= O_DIRECT; }

    int fd = ::open(filename.c_str(), flags, 0644);
    if (fd < 0) { return posix_error(filename, errno); }

    size_t buf_size =
        (buffer_size != 0) ? buffer_size : k_writable_file_buf_size;
    AlignedBuffer buf(buf_size, k_o_direct_alignment);
    *result = new PosixWritableFileWriter(filename, fd, ring, &buf, direct_io_);

    return Status::ok();
}

Status PosixEnv::new_appendable_file_writer(const std::string &filename,
                                            WritableFileWriter **result,
                                            size_t buffer_size) {
    // Calling thread's io_uring ring (nullptr when unavailable).
    struct io_uring *ring = get_thread_local_ring();

    // O_APPEND and O_DIRECT do not mix: on Linux an append-mode write ignores
    // the supplied offset, so we could never place a block-aligned write. When
    // direct I/O is requested, position writes ourselves via the tracked file
    // size instead of relying on O_APPEND. O_RDWR is required because the
    // first flush may read-modify-write a leading partial block.
    int flags = O_CREAT | kOpenBaseFlags;
    if (direct_io_) {
        flags |= O_RDWR | O_DIRECT;
    } else {
        flags |= O_WRONLY | O_APPEND;
    }

    int fd = ::open(filename.c_str(), flags, 0644);
    if (fd < 0) { return posix_error(filename, errno); }

    size_t buf_size =
        (buffer_size != 0) ? buffer_size : k_writable_file_buf_size;
    AlignedBuffer buf(buf_size, k_o_direct_alignment);

    uint64_t file_size = 0;
    Status s = get_file_size(filename, &file_size);
    if (!s.is_ok()) { return s; }

    *result = new PosixWritableFileWriter(filename, fd, ring, &buf, direct_io_,
                                          file_size);

    return Status::ok();
}

bool PosixEnv::file_exists(const std::string &filename) {
    return ::access(filename.c_str(), F_OK) == 0;
}

Status PosixEnv::get_children(const std::string &dir_path,
                              std::vector<std::string> *result) {
    result->clear();
    ::DIR *dir = ::opendir(dir_path.c_str());
    if (dir == nullptr) { return posix_error(dir_path, errno); }
    struct ::dirent *entry;
    while ((entry = ::readdir(dir)) != nullptr) {
        result->emplace_back(entry->d_name);
    }
    ::closedir(dir);
    return Status::ok();
}

Status PosixEnv::remove_file(const std::string &filename) {
    if (::unlink(filename.c_str()) != 0) {
        return posix_error(filename, errno);
    }
    return Status::ok();
}

Status PosixEnv::create_dir(const std::string &dir_name) {
    if (::mkdir(dir_name.c_str(), 0755) != 0) {
        return posix_error(dir_name, errno);
    }
    return Status::ok();
}

Status PosixEnv::remove_dir(const std::string &dir_name) {
    if (::rmdir(dir_name.c_str()) != 0) { return posix_error(dir_name, errno); }
    return Status::ok();
}

Status PosixEnv::get_file_size(const std::string &filename, uint64_t *size) {
    struct ::stat file_stat;
    if (::stat(filename.c_str(), &file_stat) != 0) {
        *size = 0;
        return posix_error(filename, errno);
    }
    *size = file_stat.st_size;
    return Status::ok();
}

Status PosixEnv::rename_file(const std::string &from, const std::string &to) {
    if (::rename(from.c_str(), to.c_str()) != 0) {
        return posix_error(from, errno);
    }
    return Status::ok();
}

Status PosixEnv::lock_file(const std::string &filename, FileLock **lock) {
    *lock = nullptr;

    int fd = ::open(filename.c_str(), O_RDWR | O_CREAT | kOpenBaseFlags, 0644);
    if (fd < 0) { return posix_error(filename, errno); }

    if (!locks_.insert(filename)) {
        ::close(fd);
        return Status::io_error("lock " + filename, "already held by process");
    }

    if (lock_or_unlock(fd, true) == -1) {
        int lock_errno = errno;
        ::close(fd);
        locks_.remove(filename);
        return posix_error("lock " + filename, lock_errno);
    }

    *lock = new PosixFileLock(fd, filename);
    return Status::ok();
}

Status PosixEnv::unlock_file(FileLock *lock) {
    PosixFileLock *posix_file_lock = static_cast<PosixFileLock *>(lock);
    if (lock_or_unlock(posix_file_lock->fd(), false) == -1) {
        return posix_error("unlock " + posix_file_lock->filename(), errno);
    }

    locks_.remove(posix_file_lock->filename());
    ::close(posix_file_lock->fd());
    delete posix_file_lock;
    return Status::ok();
}

void PosixEnv::schedule(void (*function)(void *arg), void *arg) {
    // Env::schedule reports nothing, so a rejected task (pool shutting down or
    // no workers available) is dropped. The pool owns that decision.
    pool.enqueue(function, arg);
}

Status PosixEnv::get_test_dir(std::string *result) {
    const char *env = std::getenv("TEST_TMPDIR");
    if (env && env[0] != '\0') {
        *result = env;
    } else {
        char buf[100];
        std::snprintf(buf, sizeof(buf), "/tmp/anchordbtest-%d",
                      static_cast<int>(::geteuid()));
        *result = buf;
    }

    // The CreateDir status is ignored because the directory may already
    // exist.
    create_dir(*result);

    return Status::ok();
}

Status PosixEnv::new_logger(const std::string &filename, Logger **result) {
    int fd = ::open(filename.c_str(),
                    O_APPEND | O_WRONLY | O_CREAT | kOpenBaseFlags, 0644);
    if (fd < 0) {
        *result = nullptr;
        return posix_error(filename, errno);
    }

    std::FILE *fp = ::fdopen(fd, "w");
    if (fp == nullptr) {
        ::close(fd);
        *result = nullptr;
        return posix_error(filename, errno);
    } else {
        *result = new PosixLogger(fp);
        return Status::ok();
    }
}

uint64_t PosixEnv::now_micros() {
    struct timeval tv;
    ::gettimeofday(&tv, nullptr);
    return static_cast<uint64_t>(tv.tv_sec) * 1000000 + tv.tv_usec;
}

void PosixEnv::sleep_for_micros(int micros) {
    std::this_thread::sleep_for(std::chrono::microseconds(micros));
}

} // namespace ANCHORDB_NAMESPACE
