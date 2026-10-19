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

#include "writer.h"
#include "anchordb/anchordb_namespace.h"
#include "io_uring_config.h"
#include "sync_io.h"
#include <fcntl.h>
#include <unistd.h>

namespace ANCHORDB_NAMESPACE {
Status PosixWritableFileWriter::submit_write_(const char *src, size_t len,
                                              uint64_t offset) {
    // io_uring unavailable: use pwrite(2) at the explicit offset so the
    // writer never depends on a shared file position.
    if (ring_ == nullptr) {
        return file_internal::sync_pwrite(filename_, fd_, src, len, offset);
    }

#if !HAS_IO_URING
    return Status::not_supported(
        std::string_view("io_uring support not compiled in"));
#else
    size_t total_written = 0;
    while (total_written < len) {
        struct io_uring_sqe *sqe = io_uring_get_sqe(ring_);
        if (!sqe) {
            int submit_ret = io_uring_submit(ring_);
            if (submit_ret < 0) { return posix_error(filename_, -submit_ret); }
            sqe = io_uring_get_sqe(ring_);
            if (!sqe) { return posix_error(filename_, EBUSY); }
        }

        const char *current_src = src + total_written;
        size_t current_len = len - total_written;
        uint64_t current_offset = offset + total_written;

        io_uring_prep_write(sqe, fd_, current_src, current_len, current_offset);
        io_uring_sqe_set_data(sqe, nullptr);

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

        if (res == 0) { return posix_error(filename_, ENOSPC); }

        total_written += static_cast<size_t>(res);

        if (direct_io_ && (res % k_o_direct_alignment != 0) &&
            (total_written < len)) {
            return posix_error(filename_, EINVAL);
        }
    }

    return Status::ok();
#endif
}

Status PosixWritableFileWriter::append(std::string_view data) {
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
                if (!s.is_ok()) return s;
                filesize_ += left;
                buf_offset_ = filesize_;
                return Status::ok();
            }
        }

        size_t written = buf_.append(src, left);
        src += written;
        left -= written;
        filesize_ += written;

        if (buf_.current_size() == buf_.capacity()) {
            Status s = flush_buffer();
            if (!s.is_ok()) return s;
        }
    }

    return Status::ok();
}

Status PosixWritableFileWriter::close() {
    Status s = flush_buffer();

    if (direct_io_ && s.is_ok() && fd_ >= 0) {
        if (::ftruncate(fd_, filesize_) < 0) {
            s = posix_error(filename_, errno);
        }
    }

    if (fd_ >= 0) {
        // Mark closed before the syscall so a second close() (for example
        // from the destructor) is a no-op instead of closing a reused fd.
        const int fd = fd_;
        fd_ = -1;
        if (::close(fd) < 0 && s.is_ok()) { s = posix_error(filename_, errno); }
    }

    return s;
}

Status PosixWritableFileWriter::sync() {
    Status s = flush_buffer();
    if (!s.is_ok()) return s;
    if (fd_ < 0) { return posix_error(filename_, EBADF); }

    if (::fdatasync(fd_) < 0) { return posix_error(filename_, errno); }

    return Status::ok();
}

Status PosixWritableFileWriter::flush_buffer() {
    if (buf_.current_size() == 0) return Status::ok();

    const size_t logical_bytes = buf_.current_size();

    if (!direct_io_) {
        Status s = submit_write_(buf_.data(), logical_bytes, buf_offset_);
        if (!s.is_ok()) return s;
        buf_.clear();
        buf_offset_ = filesize_;
        return Status::ok();
    }

    // O_DIRECT requires both the file offset and the transfer length to be
    // block-aligned. buf_offset_ is kept aligned (see the tail handling at the
    // bottom); the one exception is the first write to an appendable file
    // whose initial size is not aligned, handled with a read-modify-write of
    // the leading partial block.
    const char *written = buf_.data();
    uint64_t write_offset = buf_offset_;
    size_t write_len = logical_bytes;
    AlignedBuffer lead_block;

    if ((buf_offset_ % k_o_direct_alignment) != 0) {
        const uint64_t aligned_start =
            buf_offset_ & ~(static_cast<uint64_t>(k_o_direct_alignment) - 1);
        const size_t leading = static_cast<size_t>(buf_offset_ - aligned_start);
        const size_t total =
            round_up(leading + logical_bytes, k_o_direct_alignment);

        lead_block = AlignedBuffer(total, k_o_direct_alignment);
        if (!lead_block.is_allocated()) {
            return Status::io_error(std::string_view(
                "Failed to allocate aligned buffer for O_DIRECT write"));
        }

        size_t got = 0;
        Status s = file_internal::sync_pread(filename_, fd_, lead_block.data(),
                                             total, aligned_start, &got);
        if (!s.is_ok()) return s;
        if (got < total) {
            std::memset(lead_block.data() + got, 0, total - got);
        }
        std::memcpy(lead_block.data() + leading, buf_.data(), logical_bytes);
        std::memset(lead_block.data() + leading + logical_bytes, 0,
                    total - leading - logical_bytes);

        s = submit_write_(lead_block.data(), total, aligned_start);
        if (!s.is_ok()) return s;
        written = lead_block.data() + leading;
        write_offset = aligned_start;
        write_len = total;
    } else {
        const size_t padded = round_up(logical_bytes, k_o_direct_alignment);
        std::memset(buf_.data() + logical_bytes, 0, padded - logical_bytes);
        Status s = submit_write_(buf_.data(), padded, buf_offset_);
        if (!s.is_ok()) return s;
        write_len = padded;
    }

    // The aligned write may have extended the file with padding. Trim it so the
    // on-disk size always equals the logical size, keeping a reopen (and a
    // crash after sync) from mistaking padding for data.
    if (write_offset + write_len > filesize_) {
        if (::ftruncate(fd_, static_cast<off_t>(filesize_)) < 0) {
            return posix_error(filename_, errno);
        }
    }

    // Retain the sub-block tail that belongs to the last (padded) block so the
    // next flush can rewrite it at an aligned offset. This keeps the logical
    // byte stream contiguous while never issuing an unaligned O_DIRECT write.
    const size_t tail = static_cast<size_t>(filesize_ % k_o_direct_alignment);
    if (tail != 0) {
        std::memmove(buf_.data(), written + (logical_bytes - tail), tail);
        buf_.size(tail);
        buf_offset_ = filesize_ - tail;
    } else {
        buf_.clear();
        buf_offset_ = filesize_;
    }

    return Status::ok();
}

Status PosixWritableFileWriter::sync_dir_if_manifest() {
    Status status;
    if (!is_manifest_) { return status; }

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

    if (sync_success) { return Status::ok(); }
    return posix_error(fd_path, errno);
}

std::string PosixWritableFileWriter::dirname(const std::string &filename) {
    std::string::size_type separator_pos = filename.rfind('/');
    if (separator_pos == std::string::npos) { return std::string("."); }
    assert(filename.find('/', separator_pos + 1) == std::string::npos);

    return filename.substr(0, separator_pos);
}

std::string_view
PosixWritableFileWriter::basename(const std::string &filename) {
    std::string::size_type separator_pos = filename.rfind('/');
    if (separator_pos == std::string::npos) {
        return std::string_view(filename);
    }
    assert(filename.find('/', separator_pos + 1) == std::string::npos);

    return std::string_view(filename.data() + separator_pos + 1,
                            filename.length() - separator_pos - 1);
}
} // namespace ANCHORDB_NAMESPACE
