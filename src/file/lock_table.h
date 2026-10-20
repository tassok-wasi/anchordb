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

#include "anchordb/anchordb_namespace.h"
#include "anchordb/env.h"
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <set>
#include <string>

namespace ANCHORDB_NAMESPACE {

// Acquires or releases an exclusive file lock on |fd|.
inline int lock_or_unlock(int fd, bool lock) {
    errno = 0;
    struct ::flock file_lock_info;
    std::memset(&file_lock_info, 0, sizeof(file_lock_info));
    file_lock_info.l_type = (lock ? F_WRLCK : F_UNLCK);
    file_lock_info.l_whence = SEEK_SET;
    file_lock_info.l_start = 0;
    file_lock_info.l_len = 0;
    return ::fcntl(fd, F_SETLK, &file_lock_info);
}

// A held POSIX advisory lock: the fd it was taken on and the locked file name.
class PosixFileLock : public FileLock {
  public:
    PosixFileLock(int fd, std::string filename)
        : fd_(fd), filename_(std::move(filename)) {}

    int fd() const { return fd_; }
    const std::string &filename() const { return filename_; }

  private:
    const int fd_;
    const std::string filename_;
};

// In-process set of locked files. Prevents double-locking. Thread-safe.
class PosixLockTable {
  public:
    bool insert(const std::string &fname) {
        mu_.lock();
        bool succeeded = locked_files_.insert(fname).second;
        mu_.unlock();
        return succeeded;
    }

    void remove(const std::string &fname) {
        mu_.lock();
        locked_files_.erase(fname);
        mu_.unlock();
    }

  private:
    std::mutex mu_;
    std::set<std::string> locked_files_;
};

} // namespace ANCHORDB_NAMESPACE
