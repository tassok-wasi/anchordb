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

// Out-of-line definitions for the abstract Env/IO base classes.  These
// destructors are declared in the public env.h but were never defined, so any
// program that installs a concrete Env (e.g. PosixEnv) fails to link without
// them.  Defining them here also anchors each class's vtable.

#include "anchordb/anchordb_namespace.h"
#include "file_meta.h"
#include "io_uring_config.h"
#include "seqeuntial_reader.h"
#include "sync_io.h"
#include <fcntl.h>
#include <unistd.h>

namespace ANCHORDB_NAMESPACE {

Status PosixSequentialFileReader::submit_read_(void *dst, size_t len,
                                               uint64_t offset,
                                               size_t *bytes_read) {
    *bytes_read = 0;

    // io_uring unavailable: the caller supplied an explicit offset, so pread
    // is the correct synchronous primitive.
    if (ring_ == nullptr) {
        return file_internal::sync_pread(
            filename_, fd_, static_cast<char *>(dst), len, offset, bytes_read);
    }

#if !HAS_IO_URING
    return Status::not_supported(
        std::string_view("io_uring support not compiled in"));
#else
    size_t total_read = 0;
    while (total_read < len) {
        struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
        if (!sqe) {
            int submit_ret = io_uring_submit(ring_);
            if (submit_ret < 0) { return posix_error(filename_, -submit_ret); }
            sqe = io_uring_get_sqe(ring_);
            if (!sqe) { return posix_error(filename_, EBUSY); }
        }

        void *current_dst = static_cast<char *>(dst) + total_read;
        size_t current_len = len - total_read;
        uint64_t current_offset = offset + total_read;

        io_uring_prep_read(sqe, fd_, current_dst, current_len, current_offset);

        int submit_ret = io_uring_submit_and_wait(ring_, 1);
        if (submit_ret < 0) { return posix_error(filename_, -submit_ret); }

        struct io_uring_cqe *cqe = nullptr;
        int wait_ret = io_uring_wait_cqe(ring_, &cqe);
        if (wait_ret < 0) { return posix_error(filename_, -wait_ret); }

        int res = cqe->res;
        io_uring_cqe_seen(ring_, cqe);

        if (res < 0) {
            if (-res == EINTR) { continue; }
            return posix_error(filename_, -res);
        }

        if (res == 0) { break; }

        total_read += static_cast<size_t>(res);

        if (direct_io_ && (res % k_o_direct_alignment != 0)) { break; }
    }

    *bytes_read = total_read;
    return Status::ok();
#endif
}

Status PosixSequentialFileReader::read(size_t n, std::string_view *result,
                                       char *scratch) {
    *result = std::string_view();
    if (n == 0) return Status::ok();

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

            if (total_copied == n) break;
        }

        if (!direct_io_) {
            size_t bytes_read = 0;
            if (ring_ != nullptr) {
                Status s = submit_read_(scratch + total_copied,
                                        n - total_copied, offset_, &bytes_read);
                if (!s.is_ok()) return s;
            } else {
                // Killing two birds: no io_uring means buffered streaming, so
                // read(2) lets the kernel maintain the file offset/readahead.
                Status s = file_internal::sync_read(
                    filename_, fd_, scratch + total_copied, n - total_copied,
                    &bytes_read);
                if (!s.is_ok()) return s;
            }

            if (bytes_read == 0) break;
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
            if (!s.is_ok()) return s;

            total_copied += bytes_read;
            offset_ += bytes_read;

            buf_bytes_valid_ = 0;

            if (bytes_read < aligned_len) break;
            continue;
        }

        uint64_t aligned_offset =
            offset_ & ~static_cast<uint64_t>(k_o_direct_alignment - 1);
        size_t bytes_read = 0;

        Status s = submit_read_(buf_.data(), buf_.capacity(), aligned_offset,
                                &bytes_read);
        if (!s.is_ok()) return s;

        buf_file_offset_ = aligned_offset;
        buf_bytes_valid_ = bytes_read;

        if (offset_ >= buf_file_offset_ + buf_bytes_valid_) { break; }
    }

    *result = std::string_view(scratch, total_copied);
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

} // namespace ANCHORDB_NAMESPACE
