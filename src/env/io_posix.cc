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
#include "util/aligned_buffer.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <liburing.h>
#include <unistd.h>

namespace anchordb {

Status posix_error(const std::string &context, int error_number) {
    if (error_number == ENOENT) {
        return Status::not_found(context, std::strerror(error_number));
    } else {
        return Status::io_error(context, std::strerror(error_number));
    }
}

Status PosixSequentialFileReader::submit_read_(void *dst, size_t len,
                                               uint64_t offset,
                                               size_t *bytes_read) {
    *bytes_read = 0;
    size_t total_read = 0;

    while (total_read < len) {
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

        void *current_dst = static_cast<char *>(dst) + total_read;
        size_t current_len = len - total_read;
        uint64_t current_offset = offset + total_read;

        io_uring_prep_read(sqe, fd_, current_dst, current_len, current_offset);

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
                continue;
            }
            return posix_error(filename_, -res);
        }

        if (res == 0) {
            break;
        }

        total_read += static_cast<size_t>(res);

        if (direct_io_ && (res % k_o_direct_alignment != 0)) {
            break;
        }
    }

    *bytes_read = total_read;
    return Status::ok();
}

Status PosixSequentialFileReader::read(size_t n, Slice *result, char *scratch) {
    *result = Slice();
    if (n == 0)
        return Status::ok();

    size_t total_copied = 0;

    while (total_copied < n) {
        if (offset_ >= buf_file_offset_ &&
            offset_ < buf_file_offset_ + buf_bytes_valid_) {

            size_t buf_internal_offset =
                static_cast<size_t>(offset_ - buf_file_offset_);
            size_t available_bytes = buf_bytes_valid_ - buf_internal_offset;
            size_t bytes_to_copy = std::min(n - total_copied, available_bytes);

            std::memcpy(scratch + total_copied,
                        buf_.data() + buf_internal_offset, bytes_to_copy);

            total_copied += bytes_to_copy;
            offset_ += bytes_to_copy;

            if (total_copied == n)
                break;
        }

        if (!direct_io_) {
            size_t bytes_read = 0;
            if (ring_ != nullptr) {
                Status s = submit_read_(scratch + total_copied,
                                        n - total_copied, offset_, &bytes_read);
                if (!s.is_ok())
                    return s;
            } else {
                ::ssize_t r =
                    ::read(fd_, scratch + total_copied, n - total_copied);
                if (r < 0) {
                    if (errno == EINTR)
                        continue;
                    return posix_error(filename_, errno);
                }
                bytes_read = static_cast<size_t>(r);
            }

            if (bytes_read == 0)
                break;
            total_copied += bytes_read;
            offset_ += bytes_read;
            break;
        }

        size_t remaining_req = n - total_copied;
        bool is_dst_aligned =
            (reinterpret_cast<uintptr_t>(scratch + total_copied) %
                 k_o_direct_alignment ==
             0);
        bool is_offset_aligned = (offset_ % k_o_direct_alignment == 0);

        if (remaining_req >= buf_.capacity() && is_dst_aligned &&
            is_offset_aligned) {
            size_t aligned_len =
                remaining_req & ~static_cast<size_t>(k_o_direct_alignment - 1);
            size_t bytes_read = 0;

            Status s = submit_read_(scratch + total_copied, aligned_len,
                                    offset_, &bytes_read);
            if (!s.is_ok())
                return s;

            total_copied += bytes_read;
            offset_ += bytes_read;

            buf_bytes_valid_ = 0;

            if (bytes_read < aligned_len)
                break;
            continue;
        }

        uint64_t aligned_offset =
            offset_ & ~static_cast<uint64_t>(k_o_direct_alignment - 1);
        size_t bytes_read = 0;

        Status s = submit_read_(buf_.data(), buf_.capacity(), aligned_offset,
                                &bytes_read);
        if (!s.is_ok())
            return s;

        buf_file_offset_ = aligned_offset;
        buf_bytes_valid_ = bytes_read;

        if (offset_ >= buf_file_offset_ + buf_bytes_valid_) {
            break;
        }
    }

    *result = Slice(scratch, total_copied);
    return Status::ok();
}

Status PosixSequentialFileReader::skip(uint64_t n) {
    offset_ += n;

    if (offset_ < buf_file_offset_ ||
        offset_ >= buf_file_offset_ + buf_bytes_valid_) {
        buf_bytes_valid_ = 0;
    }

    if (!direct_io_ && ring_ == nullptr) {
        if (::lseek(fd_, static_cast<off_t>(offset_), SEEK_SET) ==
            static_cast<off_t>(-1)) {
            return posix_error(filename_, errno);
        }
    }

    return Status::ok();
}

Status PosixRandomAccessFileReader::submit_read_(int fd, void *buf, size_t n,
                                                 uint64_t offset,
                                                 size_t *bytes_read) {
    *bytes_read = 0;
    size_t total_read = 0;
    int sqe_count = 0;

    // Submit all read SQEs in batches bounded by ring capacity.
    // Each batch is submitted and reaped before the next batch is queued,
    // so we never have unowned CQEs floating in a shared ring.
    while (total_read < n) {
        // Determine how many SQEs we can queue in this batch.
        // io_uring_sq_space_left() returns the number of free SQE slots;
        // cap it to the remaining bytes we need to read.
        unsigned sq_space = io_uring_sq_space_left(ring_);
        if (sq_space == 0) {
            return posix_error(filename_, EBUSY);
        }

        int batch_count = 0;
        size_t batch_bytes = 0;

        while (batch_bytes < n - total_read &&
               batch_count < static_cast<int>(sq_space)) {
            struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
            if (!sqe) {
                // Should not happen since we checked sq_space_left, but be
                // safe.
                break;
            }

            void *current_dst =
                static_cast<char *>(buf) + total_read + batch_bytes;
            size_t current_len = n - total_read - batch_bytes;
            uint64_t current_offset = offset + total_read + batch_bytes;

            io_uring_prep_read(sqe, fd, current_dst, current_len,
                               current_offset);
            io_uring_sqe_set_data(sqe, nullptr);
            batch_count++;
            batch_bytes += current_len;
        }

        // Submit this batch
        int submit_ret = io_uring_submit(ring_);
        if (submit_ret < 0) {
            return posix_error(filename_, -submit_ret);
        }

        // Reap completions for this batch only
        for (int i = 0; i < batch_count; i++) {
            struct io_uring_cqe *cqe = nullptr;
            int wait_ret = io_uring_wait_cqe(ring_, &cqe);
            if (wait_ret < 0) {
                return posix_error(filename_, -wait_ret);
            }

            int res = cqe->res;
            io_uring_cqe_seen(ring_, cqe);

            if (res < 0) {
                if (-res == EINTR) {
                    i--; // retry this completion
                    continue;
                }
                return posix_error(filename_, -res);
            }

            if (res == 0) {
                // EOF reached; stop processing further batches
                total_read += static_cast<size_t>(res);
                *bytes_read = total_read;
                return Status::ok();
            }

            total_read += static_cast<size_t>(res);

            if (direct_io_ && (res % k_o_direct_alignment != 0)) {
                // Short read under O_DIRECT; stop here
                *bytes_read = total_read;
                return Status::ok();
            }
        }

        sqe_count += batch_count;
    }

    *bytes_read = total_read;
    return Status::ok();
}

Status PosixRandomAccessFileReader::read(uint64_t offset, size_t n,
                                         Slice *result, char *scratch) {
    *result = Slice();
    int target_fd = -1;

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

    // Declared at function scope so the bounce buffer outlives the
    // submit_read_ call below.
    AlignedBuffer aligned_buf;

    if (direct_io_) {
        bool is_scratch_aligned =
            (reinterpret_cast<uintptr_t>(scratch) % k_o_direct_alignment == 0);
        bool is_offset_aligned = (offset % k_o_direct_alignment == 0);
        bool is_len_aligned = (n % k_o_direct_alignment == 0);

        if (is_scratch_aligned && is_offset_aligned && is_len_aligned) {
            target_buf = scratch;
        } else {
            read_offset =
                offset & ~(static_cast<uint64_t>(k_o_direct_alignment) - 1);
            uint64_t end_offset =
                ((offset + n) + k_o_direct_alignment - 1) &
                ~(static_cast<uint64_t>(k_o_direct_alignment) - 1);
            read_bytes = static_cast<size_t>(end_offset - read_offset);

            aligned_buf = AlignedBuffer(read_bytes, k_o_direct_alignment);
            if (!aligned_buf.is_allocated()) {
                return Status::io_error(
                    "Failed to allocate sector-aligned buffer for O_DIRECT");
            }
            target_buf = aligned_buf.data();
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
            *result = Slice(scratch, 0);
            return Status::ok();
        }

        size_t actual_payload_bytes =
            std::min(static_cast<size_t>(bytes_read - internal_offset), n);

        if (target_buf != scratch) {
            std::memcpy(scratch, target_buf + internal_offset,
                        actual_payload_bytes);
        }
        *result = Slice(scratch, actual_payload_bytes);
    } else {
        *result = Slice(scratch, bytes_read);
    }

    return Status::ok();
}

Status PosixWritableFileWriter::submit_write_(const char *src, size_t len,
                                              uint64_t offset) {
    size_t total_written = 0;

    while (total_written < len) {
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

        const char *current_src = src + total_written;
        size_t current_len = len - total_written;
        uint64_t current_offset = offset + total_written;

        io_uring_prep_write(sqe, fd_, current_src, current_len, current_offset);
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
                continue;
            }
            return posix_error(filename_, -res);
        }

        if (res == 0) {
            return posix_error(filename_, ENOSPC);
        }

        total_written += static_cast<size_t>(res);

        if (direct_io_ && (res % k_o_direct_alignment != 0) &&
            (total_written < len)) {
            return posix_error(filename_, EINVAL);
        }
    }

    return Status::ok();
}

Status PosixWritableFileWriter::append(const Slice &data) {
    const char *src = data.data();
    size_t left = data.size();

    while (left > 0) {
        if (direct_io_ && buf_.current_size() == 0) {
            bool is_src_aligned =
                (reinterpret_cast<uintptr_t>(src) % k_o_direct_alignment == 0);
            bool is_offset_aligned = (filesize_ % k_o_direct_alignment == 0);
            bool is_len_aligned = (left % k_o_direct_alignment == 0);

            if (is_src_aligned && is_offset_aligned && is_len_aligned) {
                Status s = submit_write_(src, left, filesize_);
                if (!s.is_ok())
                    return s;
                filesize_ += left;
                return Status::ok();
            }
        }

        size_t written = buf_.append(src, left);
        src += written;
        left -= written;
        filesize_ += written;

        if (buf_.current_size() == buf_.capacity()) {
            Status s = flush_buffer();
            if (!s.is_ok())
                return s;
        }
    }

    return Status::ok();
}

Status PosixWritableFileWriter::close() {
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

Status PosixWritableFileWriter::sync() {
    Status s = flush_buffer();
    if (!s.is_ok())
        return s;

    if (::fdatasync(fd_) < 0) {
        return posix_error(filename_, errno);
    }

    return Status::ok();
}

Status PosixWritableFileWriter::flush_buffer() {
    if (buf_.current_size() == 0)
        return Status::ok();

    size_t bytes_to_write = buf_.current_size();
    uint64_t write_offset = filesize_ - buf_.current_size();

    if (direct_io_) {
        size_t remainder = buf_.current_size() % k_o_direct_alignment;
        if (remainder != 0) {
            size_t padded_size =
                buf_.current_size() + (k_o_direct_alignment - remainder);
            std::memset(buf_.data() + buf_.current_size(), 0,
                        k_o_direct_alignment - remainder);
            bytes_to_write = padded_size;
        }
    }

    Status s = submit_write_(buf_.data(), bytes_to_write, write_offset);
    if (!s.is_ok())
        return s;

    buf_.clear();
    return Status::ok();
}

Status PosixWritableFileWriter::sync_dir_if_manifest() {
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

Status PosixWritableFileWriter::sync_fd(int fd, const std::string &fd_path) {
    bool sync_success = ::fdatasync(fd) == 0;

    if (sync_success) {
        return Status::ok();
    }
    return posix_error(fd_path, errno);
}

std::string PosixWritableFileWriter::dirname(const std::string &filename) {
    std::string::size_type separator_pos = filename.rfind('/');
    if (separator_pos == std::string::npos) {
        return std::string(".");
    }
    assert(filename.find('/', separator_pos + 1) == std::string::npos);

    return filename.substr(0, separator_pos);
}

Slice PosixWritableFileWriter::basename(const std::string &filename) {
    std::string::size_type separator_pos = filename.rfind('/');
    if (separator_pos == std::string::npos) {
        return Slice(filename);
    }
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
    file_lock_info.l_len = 0;
    return ::fcntl(fd, F_SETLK, &file_lock_info);
}

} // namespace anchordb
