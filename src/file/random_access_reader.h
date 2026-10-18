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
#include "anchordb/status.h"
#include "limiter.h"
#include <cstdint>
#include <liburing.h>
#include <unistd.h>

namespace ANCHORDB_NAMESPACE {

// Random read via io_uring. Supports permanent or borrowed fd.
class PosixRandomAccessFileReader final : public RandomAccessFileReader {
  public:
    PosixRandomAccessFileReader(std::string filename, int fd, Limiter *limiter,
                                struct io_uring *ring, bool direct_io = true)
        : filename_(std::move(filename)), has_permanent_fd_(fd >= 0), fd_(fd),
          limiter_(limiter), ring_(ring), direct_io_(direct_io) {}

    ~PosixRandomAccessFileReader() override {
        if (has_permanent_fd_) {
            ::close(fd_);
            if (limiter_ != nullptr) { limiter_->release(); }
        }
    }

    Status read(uint64_t offset, size_t n, std::string_view *result,
                char *scratch) override;

  private:
    Status submit_read_(int fd, void *buf, size_t n, uint64_t offset,
                        size_t *bytes_read);

    const std::string filename_;
    const bool has_permanent_fd_;
    const int fd_;
    Limiter *const limiter_;
    struct io_uring *const ring_;
    const bool direct_io_;
};
} // namespace ANCHORDB_NAMESPACE
