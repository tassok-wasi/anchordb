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
#include "anchordb/status.h"
#include "file_meta.h"
#include "util/aligned_buffer.h"
#include <cstdint>
#include <liburing.h>

namespace ANCHORDB_NAMESPACE {
// Buffered sequential write via io_uring. For MANIFEST files, sync() also
// fsyncs the parent directory.
class PosixWritableFileWriter final : public WritableFileWriter {
  public:
    PosixWritableFileWriter(std::string filename, int fd, struct io_uring *ring,
                            AlignedBuffer *buf = nullptr, bool direct_io = true,
                            uint64_t initial_filesize = 0)
        : filename_(std::move(filename)), fd_(fd), ring_(ring),
          filesize_(initial_filesize), direct_io_(direct_io),
          is_manifest_(is_manifest(filename_)), dirname_(dirname(filename_)) {
        if (buf != nullptr) {
            buf_ = std::move(*buf);
        } else {
            buf_ =
                AlignedBuffer(k_writable_file_buf_size, k_o_direct_alignment);
        }
    }

    ~PosixWritableFileWriter() override {
        if (fd_ >= 0) { close(); }
    }

    Status append(std::string_view data) override;
    Status flush() override { return flush_buffer(); }
    Status close() override;
    Status sync() override;

  private:
    Status submit_write_(const char *src, size_t len, uint64_t offset);
    Status flush_buffer();
    Status sync_dir_if_manifest();
    Status sync_fd(int fd, const std::string &fd_path);

    std::string dirname(const std::string &filename);
    std::string_view basename(const std::string &filename);

    bool is_manifest(const std::string &filename) {
        return basename(filename).starts_with("MANIFEST");
    }

    const std::string filename_;
    const bool is_manifest_;
    const std::string dirname_;
    int fd_;
    struct io_uring *const ring_;
    uint64_t filesize_;
    const bool direct_io_;
    AlignedBuffer buf_;
};
} // namespace ANCHORDB_NAMESPACE
