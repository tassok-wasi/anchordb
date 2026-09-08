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

#include "anchordb/status.h"
#include "io/aligned_buf.h"
#include "io_posix.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <liburing.h>
#include <unistd.h>

namespace anchordb {

// Converts a POSIX errno value into an appropriate Status.
// ENOENT maps to Status::not_found; all other errors map to Status::io_error.
Status posix_error(const std::string &context, int error_number) {
    if (error_number == ENOENT) {
        return Status::not_found(context, std::strerror(error_number));
    } else {
        return Status::io_error(context, std::strerror(error_number));
    }
}

Status PosixSequentialReadFile::submit_read_(void *dst, size_t len,
                                             uint64_t offset,
                                             size_t *bytes_read) {
    *bytes_read = 0;

    struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
    if (!sqe) {
        int submit_ret = io_uring_submit(ring_);
        if (submit_ret < 0) {
            return posix_error(filename_, -submit_ret);
        }
        sqe = io_uring_get_sqe(ring_);
        if (!sqe) {
            return posix_error(filename_, EBUSY);
        }
    }

    // Prepare positional read at explicit target offset
    io_uring_prep_read(sqe, fd_, dst, len, offset);
    io_uring_sqe_set_data(sqe, nullptr);

    int submit_ret = io_uring_submit_and_wait(ring_, 1);
    if (submit_ret < 0) {
        return posix_error(filename_, -submit_ret);
    }

    struct io_uring_cqe *cqe = nullptr;
    int wait_ret = io_uring_wait_cqe(ring_, &cqe);
    if (wait_ret < 0) {
        return posix_error(filename_, -wait_ret);
    }

    int res = cqe->res;
    io_uring_cqe_seen(ring_, cqe);

    if (res < 0) {
        if (-res == EINTR) {
            return submit_read_(dst, len, offset,
                                bytes_read); // Retry interrupted read
        }
        return posix_error(filename_, -res);
    }

    *bytes_read = static_cast<size_t>(res);
    return Status::ok();
}

// Reads up to n bytes from the file descriptor into scratch, retrying on EINTR.
// On success, *result is set to a Slice over the bytes actually read.
Status PosixSequentialReadFile::read(size_t n, Slice *result, char *scratch) {
    *result = Slice();
    if (n == 0)
        return Status::ok();

    // If memory pointer, file offset, and length are all 4096-byte aligned
    if (direct_io_) {
        bool is_dst_aligned =
            (reinterpret_cast<uintptr_t>(scratch) % 4096 == 0);
        bool is_offset_aligned = (offset_ % 4096 == 0);
        bool is_len_aligned = (n % 4096 == 0);

        if (is_dst_aligned && is_offset_aligned && is_len_aligned) {
            size_t bytes_read = 0;
            Status s = submit_read_(scratch, n, offset_, &bytes_read);
            if (!s.is_ok())
                return s;

            offset_ += bytes_read;
            *result = Slice(scratch, bytes_read);
            return Status::ok();
        }
    }

    // Used when scratch buffer or request size 'n' is not 4KB aligned
    if (direct_io_) {
        size_t total_read = 0;

        while (total_read < n) {
            uint64_t aligned_offset =
                offset_ & ~4095ULL; // Round down to 4KB sector boundary
            size_t sector_diff = static_cast<size_t>(offset_ - aligned_offset);

            size_t read_capacity = staging_buf_->capacity();
            size_t bytes_read = 0;

            Status s = submit_read_(staging_buf_->data(), read_capacity,
                                    aligned_offset, &bytes_read);
            if (!s.is_ok())
                return s;

            if (bytes_read <= sector_diff) {
                break; // EOF reached
            }

            size_t valid_bytes = bytes_read - sector_diff;
            size_t bytes_to_copy = std::min(n - total_read, valid_bytes);

            std::memcpy(scratch + total_read,
                        staging_buf_->data() + sector_diff, bytes_to_copy);

            total_read += bytes_to_copy;
            offset_ += bytes_to_copy;

            if (bytes_read < read_capacity) {
                break; // Reached end of file
            }
        }

        *result = Slice(scratch, total_read);
        return Status::ok();
    }

    // BUFFERED / NON-DIRECT FALLBACK PATH (Standard POSIX / io_uring)
    if (ring_ != nullptr) {
        size_t bytes_read = 0;
        Status s = submit_read_(scratch, n, offset_, &bytes_read);
        if (!s.is_ok())
            return s;

        offset_ += bytes_read;
        *result = Slice(scratch, bytes_read);
        return Status::ok();
    } else {
        // POSIX read fallback
        while (true) {
            ::ssize_t read_size = ::read(fd_, scratch, n);
            if (read_size < 0) {
                if (errno == EINTR)
                    continue;
                return posix_error(filename_, errno);
            }
            offset_ += read_size;
            *result = Slice(scratch, read_size);
            break;
        }
    }

    return Status::ok();
}

// Advances the file offset by n bytes relative to the current position.
Status PosixSequentialReadFile::skip(uint64_t n) {
    // Advance internal logical offset
    offset_ += n;

    // Sync OS kernel offset pointer if fallback POSIX read() is used
    if (!direct_io_ && ring_ == nullptr) {
        if (::lseek(fd_, static_cast<off_t>(n), SEEK_CUR) ==
            static_cast<off_t>(-1)) {
            return posix_error(filename_, errno);
        }
    }

    return Status::ok();
}

Status PosixRandomAccessFile::submit_read_(int fd, void *buf, size_t n,
                                           uint64_t offset,
                                           size_t *bytes_read) {
    *bytes_read = 0;

    // Fetch SQE slot from the injected io_uring instance
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
    if (!sqe) {
        // Queue full: flush pending items to kernel and retry
        int submit_ret = io_uring_submit(ring_);
        if (submit_ret < 0) {
            return posix_error(filename_, -submit_ret);
        }
        sqe = io_uring_get_sqe(ring_);
        if (!sqe) {
            return posix_error(filename_, EBUSY);
        }
    }

    // Prepare non-blocking positional read
    io_uring_prep_read(sqe, fd, buf, n, offset);
    io_uring_sqe_set_data(sqe, nullptr);

    // Submit and wait for completion of 1 I/O event
    int submit_ret = io_uring_submit_and_wait(ring_, 1);
    if (submit_ret < 0) {
        return posix_error(filename_, -submit_ret);
    }

    // Reap Completion Queue Event (CQE)
    struct io_uring_cqe *cqe = nullptr;
    int wait_ret = io_uring_wait_cqe(ring_, &cqe);
    if (wait_ret < 0) {
        return posix_error(filename_, -wait_ret);
    }

    int res = cqe->res;
    io_uring_cqe_seen(ring_, cqe);

    if (res < 0) {
        return posix_error(filename_, -res);
    }

    *bytes_read = static_cast<size_t>(res);
    return Status::ok();
}

// Performs a positional read at the given offset using io_uring.
// If the file was opened with a permanent fd it is reused; otherwise a
// temporary fd is opened for the duration of the call and closed on return.
Status PosixRandomAccessFile::read(uint64_t offset, size_t n, Slice *result,
                                   char *scratch) {
    *result = Slice();
    int target_fd = -1;

    // Resolve active FD (Permanent vs. On-Demand Temporary)
    if (has_permanent_fd_) {
        target_fd = fd_;
    } else {
        int flags = O_RDONLY | O_CLOEXEC;
        if (direct_io_) {
            flags |= O_DIRECT;
        }
        target_fd = ::open(filename_.c_str(), flags, 0644);
        if (target_fd < 0) {
            return posix_error(filename_, errno);
        }
    }

    // RAII cleanup guard for temporary FDs
    struct TempFdGuard {
        int fd;
        bool is_temp;
        ~TempFdGuard() {
            if (is_temp && fd >= 0) {
                ::close(fd);
            }
        }
    } guard{target_fd, !has_permanent_fd_};

    uint64_t read_offset = offset;
    size_t read_bytes = n;
    char *target_buf = scratch;
    DefaultAlignedBuffer buf;

    if (direct_io_) {
        bool is_scratch_aligned =
            (reinterpret_cast<uintptr_t>(scratch) % k_direct_io_alignment == 0);
        bool is_offset_aligned = (offset % k_direct_io_alignment == 0);
        bool is_len_aligned = (n % k_direct_io_alignment == 0);

        if (is_scratch_aligned && is_offset_aligned && is_len_aligned) {
            target_buf = scratch;
        } else {
            read_offset =
                offset & ~(static_cast<uint64_t>(k_direct_io_alignment) - 1);
            uint64_t end_offset =
                ((offset + n) + k_direct_io_alignment - 1) &
                ~(static_cast<uint64_t>(k_direct_io_alignment) - 1);
            read_bytes = static_cast<size_t>(end_offset - read_offset);

            buf.allocate(read_bytes, k_direct_io_alignment);
            if (!buf.is_allocated()) {
                return Status::io_error(
                    "Failed to allocate sector-aligned buffer for O_DIRECT");
            }
            target_buf = buf.data();
        }
    }

    size_t bytes_read = 0;
    Status status = submit_read_(target_fd, target_buf, read_bytes, read_offset,
                                 &bytes_read);
    if (!status.is_ok()) {
        return status;
    }

    if (direct_io_) {
        uint64_t internal_offset = offset - read_offset;

        if (bytes_read <= internal_offset) {
            // EOF reached before requested data offset
            *result = Slice(scratch, 0);
            return Status::ok();
        }

        size_t actual_payload_bytes =
            std::min(static_cast<size_t>(bytes_read - internal_offset), n);

        if (target_buf != scratch) {
            // Copy relevant window into user's scratch buffer
            std::memcpy(scratch, target_buf + internal_offset,
                        actual_payload_bytes);
        }
        *result = Slice(scratch, actual_payload_bytes);
    } else {
        *result = Slice(scratch, bytes_read);
    }

    return Status::ok();
}

Status PosixWritableFile::submit_write_(const char *src, size_t len,
                                        uint64_t offset) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
    if (!sqe) {
        int submit_ret = io_uring_submit(ring_);
        if (submit_ret < 0) {
            return posix_error(filename_, -submit_ret);
        }
        sqe = io_uring_get_sqe(ring_);
        if (!sqe) {
            return posix_error(filename_, EBUSY);
        }
    }

    io_uring_prep_write(sqe, fd_, src, len, offset);
    io_uring_sqe_set_data(sqe, nullptr);

    int submit_ret = io_uring_submit_and_wait(ring_, 1);
    if (submit_ret < 0) {
        return posix_error(filename_, -submit_ret);
    }

    struct io_uring_cqe *cqe = nullptr;
    int wait_ret = io_uring_wait_cqe(ring_, &cqe);
    if (wait_ret < 0) {
        return posix_error(filename_, -wait_ret);
    }

    int res = cqe->res;
    io_uring_cqe_seen(ring_, cqe);

    if (res < 0) {
        return posix_error(filename_, -res);
    }

    return Status::ok();
}

// Appends data to the file, buffering small writes and flushing when the
// internal buffer is full. Large writes bypass the buffer entirely.
Status PosixWritableFile::append(const Slice &data) {
    const char *src = data.data();
    size_t left = data.size();

    while (left > 0) {
        // Zero-Copy Direct I/O Fast Path
        if (direct_io_ && buf_used_ == 0) {
            bool is_src_aligned =
                (reinterpret_cast<uintptr_t>(src) % 4096 == 0);
            bool is_offset_aligned = (filesize_ % 4096 == 0);
            bool is_len_aligned = (left % 4096 == 0);

            if (is_src_aligned && is_offset_aligned && is_len_aligned) {
                Status s = submit_write_(src, left, filesize_);
                if (!s.is_ok())
                    return s;
                filesize_ += left;
                return Status::ok();
            }
        }

        // Staging Path (through polymorphic AlignedBuffer base class
        // interface)
        size_t buf_space = buf_->capacity() - buf_used_;
        size_t bytes_to_copy = std::min(left, buf_space);

        std::memcpy(buf_->data() + buf_used_, src, bytes_to_copy);
        buf_used_ += bytes_to_copy;
        src += bytes_to_copy;
        left -= bytes_to_copy;
        filesize_ += bytes_to_copy;

        if (buf_used_ == buf_->capacity()) {
            Status s = flush_buffer();
            if (!s.is_ok())
                return s;
        }
    }

    return Status::ok();
}

// Flushes any buffered data and closes the underlying file descriptor.
Status PosixWritableFile::close() {
    Status s = flush_buffer();

    if (direct_io_ && s.is_ok()) {
        if (::ftruncate(fd_, filesize_) < 0) {
            s = posix_error(filename_, errno);
        }
    }

    if (fd_ >= 0) {
        if (::close(fd_) < 0 && s.is_ok()) {
            s = posix_error(filename_, errno);
        }
    }

    return s;
}

// Flushes buffered data and synchronises both the file and (if this is the
// manifest) its parent directory to durable storage.
Status PosixWritableFile::sync() {
    Status s = flush_buffer();
    if (!s.is_ok())
        return s;

    if (::fdatasync(fd_) < 0) {
        return posix_error(filename_, errno);
    }

    return Status::ok();
}

Status PosixWritableFile::flush_buffer() {
    if (buf_used_ == 0)
        return Status::ok();

    size_t bytes_to_write = buf_used_;
    uint64_t write_offset = filesize_ - buf_used_;

    if (direct_io_) {
        size_t remainder = buf_used_ % 4096;
        if (remainder != 0) {
            size_t padded_size = buf_used_ + (4096 - remainder);
            std::memset(buf_->data() + buf_used_, 0, 4096 - remainder);
            bytes_to_write = padded_size;
        }
    }

    Status s = submit_write_(buf_->data(), bytes_to_write, write_offset);
    if (!s.is_ok())
        return s;

    buf_used_ = 0;
    return Status::ok();
}

// If this file is the manifest, fsync the parent directory so that the
// directory entry is durable.
Status PosixWritableFile::sync_dir_if_manifest() {
    Status status;
    if (!is_manifest_) {
        return status;
    }

    int fd = ::open(dirname_.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        status = posix_error(dirname_, errno);
    } else {
        status = sync_fd(fd, dirname_);
        ::close(fd);
    }
    return status;
}

// Issues an fdatasync on the given file descriptor, returning an appropriate
// Status on failure.
Status PosixWritableFile::sync_fd(int fd, const std::string &fd_path) {
    bool sync_success = ::fdatasync(fd) == 0;

    if (sync_success) {
        return Status::ok();
    }
    return posix_error(fd_path, errno);
}

// Returns the directory component of a path.  If no '/' is present, returns
// ".".
std::string PosixWritableFile::dirname(const std::string &filename) {
    std::string::size_type separator_pos = filename.rfind('/');
    if (separator_pos == std::string::npos) {
        return std::string(".");
    }
    // The filename component should not contain a path separator. If it
    // does, the splitting was done incorrectly.
    assert(filename.find('/', separator_pos + 1) == std::string::npos);

    return filename.substr(0, separator_pos);
}

// Returns the final component of a path (everything after the last '/').
Slice PosixWritableFile::basename(const std::string &filename) {
    std::string::size_type separator_pos = filename.rfind('/');
    if (separator_pos == std::string::npos) {
        return Slice(filename);
    }
    // The filename component should not contain a path separator. If it
    // does, the splitting was done incorrectly.
    assert(filename.find('/', separator_pos + 1) == std::string::npos);

    return Slice(filename.data() + separator_pos + 1,
                 filename.length() - separator_pos - 1);
}

int lock_or_unlock(int fd, bool lock) {
    errno = 0;
    struct ::flock file_lock_info;
    std::memset(&file_lock_info, 0, sizeof(file_lock_info));
    file_lock_info.l_type = (lock ? F_WRLCK : F_UNLCK);
    file_lock_info.l_whence = SEEK_SET;
    file_lock_info.l_start = 0;
    file_lock_info.l_len = 0; // Lock/unlock entire file.
    return ::fcntl(fd, F_SETLK, &file_lock_info);
}

} // namespace anchordb
