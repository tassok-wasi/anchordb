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

#include "anchordb/anchordb_namespace.h"
#include "file_meta.h"
#include "io_uring_config.h" // IWYU pragma: keep
#include "random_access_reader.h"
#include "sync_io.h"
#include "util/aligned_buffer.h"
#include <fcntl.h>
#include <string_view>
#include <unistd.h>

namespace ANCHORDB_NAMESPACE {
Status PosixRandomAccessFileReader::submit_read_(int fd, void *buf, size_t n,
                                                 uint64_t offset,
                                                 size_t *bytes_read) {
    *bytes_read = 0;
#if !HAS_IO_URING
    (void)fd;
    (void)buf;
    (void)n;
    (void)offset;
    return Status::not_supported(
        std::string_view("io_uring support not compiled in"));
#else
    size_t total_read = 0;
    int sqe_count = 0;

    // Submit reads in batches bounded by ring capacity, reaping each batch
    // before queuing the next so no CQEs are left unowned.
    while (total_read < n) {
        // Free SQE slots available for this batch.
        unsigned sq_space = io_uring_sq_space_left(ring_);
        if (sq_space == 0) { return posix_error(filename_, EBUSY); }

        int batch_count = 0;
        size_t batch_bytes = 0;

        while (batch_bytes < n - total_read &&
               batch_count < static_cast<int>(sq_space)) {
            struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
            if (!sqe) {
                // Guarded by sq_space, but stay safe.
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
        if (submit_ret < 0) { return posix_error(filename_, -submit_ret); }

        // Reap completions for this batch only
        for (int i = 0; i < batch_count; i++) {
            struct io_uring_cqe *cqe = nullptr;
            int wait_ret = io_uring_wait_cqe(ring_, &cqe);
            if (wait_ret < 0) { return posix_error(filename_, -wait_ret); }

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
#endif
}

Status PosixRandomAccessFileReader::read(uint64_t offset, size_t n,
                                         std::string_view *result,
                                         char *scratch) {
    *result = std::string_view();
    int target_fd = -1;

    if (has_permanent_fd_) {
        target_fd = fd_;
    } else {
        int flags = O_RDONLY | O_CLOEXEC;
        if (direct_io_) { flags |= O_DIRECT; }
        target_fd = ::open(filename_.c_str(), flags, 0644);
        if (target_fd < 0) { return posix_error(filename_, errno); }
    }

    struct TempFdGuard {
        int fd;
        bool is_temp;
        ~TempFdGuard() {
            if (is_temp && fd >= 0) { ::close(fd); }
        }
    } guard{target_fd, !has_permanent_fd_};

    uint64_t read_offset = offset;
    size_t read_bytes = n;
    char *target_buf = scratch;

    // Function scope so the bounce buffer outlives submit_read_().
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
                return Status::io_error(std::string_view(
                    "Failed to allocate sector-aligned buffer for O_DIRECT"));
            }
            target_buf = aligned_buf.data();
        }
    }

    size_t bytes_read = 0;
    Status status;
    if (ring_ == nullptr) {
        // No io_uring: pread(2) is positional and thread-safe.
        status =
            file_internal::sync_pread(filename_, target_fd, target_buf,
                                      read_bytes, read_offset, &bytes_read);
        if (!status.is_ok()) { return status; }
    } else {
        status = submit_read_(target_fd, target_buf, read_bytes, read_offset,
                              &bytes_read);
        if (!status.is_ok()) { return status; }
    }

    if (direct_io_) {
        uint64_t internal_offset = offset - read_offset;

        if (bytes_read <= internal_offset) {
            *result = std::string_view(scratch, 0);
            return Status::ok();
        }

        size_t actual_payload_bytes =
            std::min(static_cast<size_t>(bytes_read - internal_offset), n);

        if (target_buf != scratch) {
            std::memcpy(scratch, target_buf + internal_offset,
                        actual_payload_bytes);
        }
        *result = std::string_view(scratch, actual_payload_bytes);
    } else {
        *result = std::string_view(scratch, bytes_read);
    }

    return Status::ok();
}
} // namespace ANCHORDB_NAMESPACE
