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

// Synchronous POSIX I/O used when io_uring is unavailable.
//
// The access pattern determines the syscall:
//   - sequential streaming  -> read()
//   - random/positional     -> pread()/pwrite()
//
// All helpers retry EINTR and preserve the number of bytes transferred when a
// later syscall fails, so callers can distinguish EOF from an error.

#include "anchordb/anchordb_namespace.h"
#include "anchordb/status.h"
#include "file/file_meta.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unistd.h>

namespace ANCHORDB_NAMESPACE {

namespace file_internal {

// Reads up to |n| bytes from the current file offset with read(2).
inline Status sync_read(const std::string &filename, int fd, char *dst,
                        size_t n, size_t *bytes_read) {
    *bytes_read = 0;
    while (*bytes_read < n) {
        ::ssize_t r = ::read(fd, dst + *bytes_read, n - *bytes_read);
        if (r < 0) {
            if (errno == EINTR) { continue; }
            return posix_error(filename, errno);
        }
        if (r == 0) { break; }
        *bytes_read += static_cast<size_t>(r);
    }
    return Status::ok();
}

// Reads up to |n| bytes at an absolute |offset| with pread(2).
inline Status sync_pread(const std::string &filename, int fd, char *dst,
                         size_t n, uint64_t offset, size_t *bytes_read) {
    *bytes_read = 0;
    while (*bytes_read < n) {
        ::ssize_t r = ::pread(fd, dst + *bytes_read, n - *bytes_read,
                              static_cast<off_t>(offset + *bytes_read));
        if (r < 0) {
            if (errno == EINTR) { continue; }
            return posix_error(filename, errno);
        }
        if (r == 0) { break; }
        *bytes_read += static_cast<size_t>(r);
    }
    return Status::ok();
}

// Writes all |n| bytes at an absolute |offset| with pwrite(2).
inline Status sync_pwrite(const std::string &filename, int fd, const char *src,
                          size_t n, uint64_t offset) {
    size_t written = 0;
    while (written < n) {
        ::ssize_t r = ::pwrite(fd, src + written, n - written,
                               static_cast<off_t>(offset + written));
        if (r < 0) {
            if (errno == EINTR) { continue; }
            return posix_error(filename, errno);
        }
        if (r == 0) { return posix_error(filename, ENOSPC); }
        written += static_cast<size_t>(r);
    }
    return Status::ok();
}

} // namespace file_internal

} // namespace ANCHORDB_NAMESPACE
