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

#pragma once

#include "anchordb/anchordb_namespace.h"
#include "anchordb/env.h"
#include "file_meta.h"
#include "io_uring_config.h"
#include "util/aligned_buffer.h"
#include <unistd.h>

namespace ANCHORDB_NAMESPACE {

// Sequential read via io_uring. Not thread-safe.
class PosixSequentialFileReader final : public SequentialFileReader {
  public:
    PosixSequentialFileReader(std::string filename, int fd,
                              struct io_uring *ring,
                              AlignedBuffer *buf = nullptr,
                              size_t buffer_size = 0, bool direct_io = false)
        : fd_(fd), filename_(std::move(filename)), ring_(ring), offset_(0),
          direct_io_(direct_io), buf_file_offset_(0), buf_bytes_valid_(0) {
        if (buf != nullptr) {
            buf_ = std::move(*buf);
        } else if (direct_io_) {
            size_t buf_size =
                (buffer_size != 0) ? buffer_size : k_seq_file_reader_buf_size;
            buf_ = AlignedBuffer(buf_size, k_o_direct_alignment);
        }
    }

    ~PosixSequentialFileReader() override {
        if (fd_ >= 0) { ::close(fd_); }
    }

    Status read(size_t n, std::string_view *result, char *scratch) override;
    Status skip(uint64_t n) override;

  private:
    Status submit_read_(void *dst, size_t len, uint64_t offset,
                        size_t *bytes_read);

    const int fd_;
    const std::string filename_;
    struct io_uring *const ring_;
    uint64_t offset_;
    const bool direct_io_;

    AlignedBuffer buf_;
    uint64_t buf_file_offset_;
    size_t buf_bytes_valid_;
};
} // namespace ANCHORDB_NAMESPACE
