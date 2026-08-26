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

#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <string>
#include <vector>

namespace anchordb {

enum class Flag {
#if defined(_WIN32)
    ReadOnly = _O_RDONLY | _O_BINARY,
    WriteOnly = _O_WRONLY | _O_CREAT | _O_BINARY,
    Append = _O_WRONLY | _O_CREAT | _O_APPEND | _O_BINARY,
    Truncate = _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY
#else
    ReadOnly = O_RDONLY,
    WriteOnly = O_WRONLY | O_CREAT,
    Append = O_WRONLY | O_CREAT | O_APPEND,
    Truncate = O_WRONLY | O_CREAT | O_TRUNC
#endif
};

class LogWriter {
  public:
    LogWriter() = default;
    ~LogWriter();

    LogWriter(const LogWriter &) = delete;
    LogWriter &operator=(const LogWriter &) = delete;

    // Opens (or creates) `file` using the given flags and permissions.
    void init(const std::string &file, Flag flag, int perm);

    size_t size() const;

    // Reads the file from `offset` through end of file and returns the bytes.
    std::vector<uint8_t> read(size_t offset);

    // Appends up to `length` bytes from `buffer` to the file.
    void append(const std::vector<uint8_t> &buffer, size_t length);

    // Closes the file and removes it from the filesystem.
    void destroy(const char *file);

  private:
    void close_();
    void set_file_size();

    int fd_ = -1;
    size_t file_size_ = 0;
};

} // namespace anchordb
