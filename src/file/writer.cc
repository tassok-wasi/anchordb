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
#include "writer.h"
#include <fcntl.h>
#include <liburing.h>
#include <unistd.h>

namespace ANCHORDB_NAMESPACE {
Status PosixWritableFileWriter::submit_write_(const char *src, size_t len,
                                              uint64_t offset) {
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
    if (!s.is_ok()) return s;

    if (::fdatasync(fd_) < 0) { return posix_error(filename_, errno); }

    return Status::ok();
}

Status PosixWritableFileWriter::flush_buffer() {
    if (buf_.current_size() == 0) return Status::ok();

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
    if (!s.is_ok()) return s;

    buf_.clear();
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
