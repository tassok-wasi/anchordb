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

// Shared POSIX file constants (I/O buffer sizes, O_DIRECT alignment) and the
// errno-to-Status helper used across the file layer.

#pragma once

#include "anchordb/anchordb_namespace.h"
#include "anchordb/status.h"
#include <cerrno>
#include <cstddef>
#include <cstring>

namespace ANCHORDB_NAMESPACE {

constexpr const size_t k_writable_file_buf_size = 65536;           // 64KB
constexpr const size_t k_seq_file_reader_buf_size = 4194304;       // 4MB
constexpr const size_t k_seq_blob_file_reader_buf_size = 67108864; // 64MB
constexpr const size_t k_o_direct_alignment = 4096;

inline Status posix_error(const std::string &context, int error_number) {
    if (error_number == ENOENT) {
        return Status::not_found(context, std::strerror(error_number));
    } else {
        return Status::io_error(context, std::strerror(error_number));
    }
}

} // namespace ANCHORDB_NAMESPACE
