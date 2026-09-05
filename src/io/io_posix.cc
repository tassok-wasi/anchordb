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
#include "io_posix.h"
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

// Reads up to n bytes from the file descriptor into scratch, retrying on EINTR.
// On success, *result is set to a Slice over the bytes actually read.
Status PosixSequentialReadFile::read(size_t n, Slice *result, char *scratch) {
    Status status;
    while (true) {
        ::ssize_t read_size = ::read(fd_, scratch, n);
        if (read_size < 0) {
            if (errno == EINTR) {
                continue; // Interrupted by signal; retry the read.
            }
            status = posix_error(filename_, errno);
            break;
        }
        *result = Slice(scratch, read_size);
        break;
    }
    return status;
}

// Advances the file offset by n bytes relative to the current position.
Status PosixSequentialReadFile::skip(uint64_t n) {
    if (::lseek(fd_, n, SEEK_CUR) == static_cast<off_t>(-1)) {
        return posix_error(filename_, errno);
    }
    return Status::ok();
}

// Performs a positional read at the given offset using io_uring.
// If the file was opened with a permanent fd it is reused; otherwise a
// temporary fd is opened for the duration of the call and closed on return.
Status PosixRandomAccessFile::read(uint64_t offset, size_t n, Slice *result,
                                   char *scratch) const {
    *result = Slice();
    int target_fd = -1;

    // Resolve active FD (Permanent vs. On-Demand Temporary)
    if (has_permanent_fd_) {
        target_fd = fd_;
    } else {
        int flags = O_RDONLY;
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
    io_uring_prep_read(sqe, target_fd, scratch, n, offset);
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

    // Return populated slice
    size_t bytes_read = static_cast<size_t>(res);
    *result = Slice(scratch, bytes_read);

    return Status::ok();
}

// Appends data to the file, buffering small writes and flushing when the
// internal buffer is full. Large writes bypass the buffer entirely.
Status PosixWritableFile::append(const Slice &data) {
    size_t write_size = data.size();
    const char *write_data = data.data();

    // Fit as much as possible into buffer.
    size_t copy_size = std::min(write_size, writable_file_buffer_size - pos_);
    std::memcpy(buf_ + pos_, write_data, copy_size);
    write_data += copy_size;
    write_size -= copy_size;
    pos_ += copy_size;
    if (write_size == 0) {
        return Status::ok();
    }

    // Can't fit in buffer, so need to do at least one write.
    Status status = flush_buffer();
    if (!status.is_ok()) {
        return status;
    }

    // Small writes go to buffer, large writes are written directly.
    if (write_size < writable_file_buffer_size) {
        std::memcpy(buf_, write_data, write_size);
        pos_ = write_size;
        return Status::ok();
    }
    return write_unbuffered(write_data, write_size);
}

// Flushes any buffered data and closes the underlying file descriptor.
Status PosixWritableFile::close() {
    Status status = flush_buffer();
    const int close_result = ::close(fd_);
    if (close_result < 0 && status.is_ok()) {
        status = posix_error(filename_, errno);
    }
    fd_ = -1;
    return status;
}

// Flushes buffered data and synchronises both the file and (if this is the
// manifest) its parent directory to durable storage.
Status PosixWritableFile::sync() {
    Status status = sync_dir_if_manifest();
    if (!status.is_ok()) {
        return status;
    }

    status = flush_buffer();
    if (!status.is_ok()) {
        return status;
    }

    return sync_fd(fd_, filename_);
}

// Writes data directly to the file descriptor, retrying on EINTR until all
// bytes have been written or a fatal error occurs.
Status PosixWritableFile::write_unbuffered(const char *data, size_t size) {
    while (size > 0) {
        ssize_t write_result = ::write(fd_, data, size);
        if (write_result < 0) {
            if (errno == EINTR) {
                continue; // Retry
            }
            return posix_error(filename_, errno);
        }
        data += write_result;
        size -= write_result;
    }
    return Status::ok();
}

// If this file is the manifest, fsync the parent directory so that the
// directory entry is durable.
Status PosixWritableFile::sync_dir_if_manifest() {
    Status status;
    if (!is_manifest_) {
        return status;
    }

    int fd = ::open(dirname_.c_str(), O_RDONLY);
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
