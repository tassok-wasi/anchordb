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

#include "log_writer.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <climits>
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace anchordb {
namespace {

// Thin wrappers around the platform file APIs so the LogWriter methods can be
// shared across Linux, macOS, and Windows.
#if defined(_WIN32)

using file_size_t = __int64;

inline int open_file(const std::string &path, int flags, int mode) {
    return _open(path.c_str(), flags, mode);
}

inline int close_file(int fd) { return _close(fd); }

// Reads at a specific offset without advancing the file position.
inline int64_t read_file(int fd, char *buf, size_t count, file_size_t offset) {
    if (_lseeki64(fd, offset, SEEK_SET) == -1)
        return -1;
    const unsigned int chunk = static_cast<unsigned int>(
        count > static_cast<size_t>(UINT_MAX) ? static_cast<size_t>(UINT_MAX)
                                              : count);
    const int n = _read(fd, buf, chunk);
    return n == -1 ? -1 : static_cast<int64_t>(n);
}

inline int64_t write_file(int fd, const char *buf, size_t count) {
    const unsigned int chunk = static_cast<unsigned int>(
        count > static_cast<size_t>(UINT_MAX) ? static_cast<size_t>(UINT_MAX)
                                              : count);
    const int n = _write(fd, buf, chunk);
    return n == -1 ? -1 : static_cast<int64_t>(n);
}

inline int sync_file(int fd) { return _commit(fd); }

inline int unlink_file(const char *path) { return _unlink(path); }

inline file_size_t stat_size(int fd) {
    struct _stati64 st;
    if (_fstati64(fd, &st) != 0)
        return -1;
    return st.st_size;
}

#else

using file_size_t = off_t;

inline int open_file(const std::string &path, int flags, int mode) {
    return ::open(path.c_str(), flags, mode);
}

inline int close_file(int fd) { return ::close(fd); }

inline int64_t read_file(int fd, char *buf, size_t count, file_size_t offset) {
    const ssize_t n = ::pread(fd, buf, count, offset);
    return n < 0 ? -1 : static_cast<int64_t>(n);
}

inline int64_t write_file(int fd, const char *buf, size_t count) {
    const ssize_t n = ::write(fd, buf, count);
    return n < 0 ? -1 : static_cast<int64_t>(n);
}

inline int sync_file(int fd) { return ::fsync(fd); }

inline int unlink_file(const char *path) { return ::unlink(path); }

inline file_size_t stat_size(int fd) {
    struct stat st;
    if (::fstat(fd, &st) != 0)
        return -1;
    return st.st_size;
}

#endif

} // namespace

LogWriter::~LogWriter() { close_(); }

void LogWriter::init(const std::string &file, Flag flag, int perm) {
    close_();

    const int fd = open_file(file, static_cast<int>(flag), perm);
    fd_ = fd;
    if (fd_ != -1)
        set_file_size();
}

void LogWriter::close_() {
    if (fd_ != -1) {
        close_file(fd_);
        fd_ = -1;
    }
}

void LogWriter::set_file_size() {
    if (fd_ == -1)
        return;

    const file_size_t size = stat_size(fd_);
    if (size < 0)
        return;

    file_size_ = static_cast<size_t>(size);
}

size_t LogWriter::size() const { return file_size_; }

std::vector<uint8_t> LogWriter::read(size_t offset) {
    if (fd_ == -1)
        return {};

    set_file_size();

    if (offset >= file_size_)
        return {};

    const size_t length = file_size_ - offset;
    std::vector<uint8_t> buffer(length);

    size_t total = 0;
    while (total < length) {
        const int64_t n = read_file(
            fd_, reinterpret_cast<char *>(buffer.data()) + total,
            length - total,
            static_cast<file_size_t>(offset) + static_cast<file_size_t>(total));

        if (n < 0)
            return {};
        if (n == 0)
            break;

        total += static_cast<size_t>(n);
    }

    buffer.resize(total);
    return buffer;
}

void LogWriter::append(const std::vector<uint8_t> &buffer, size_t length) {
    if (fd_ == -1)
        return;
    if (length > buffer.size())
        length = buffer.size();

    size_t total = 0;
    while (total < length) {
        const int64_t n = write_file(
            fd_, reinterpret_cast<const char *>(buffer.data()) + total,
            length - total);

        if (n <= 0)
            return;

        total += static_cast<size_t>(n);
    }

    sync_file(fd_);
    set_file_size();
}

void LogWriter::destroy(const char *file) {
    close_();
    if (file != nullptr)
        unlink_file(file);
}

} // namespace anchordb
