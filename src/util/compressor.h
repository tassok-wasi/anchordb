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

#include "anchordb/options.h"

#include <cstddef>
#include <string>

namespace anchordb {

// Stateless helpers for compressing and uncompressing blocks. Every method
// returns true on success and false if the operation fails or the requested
// compression type is not compiled in (see HAS_SNAPPY / HAS_ZSTD in
// port/port.h).
class Compressor {
  public:
    // Compresses [input, input + length) into *output. `level` is only
    // meaningful for compression types that support it (e.g. Zstd).
    bool compress(const char *input, size_t length, std::string *output,
                  CompressionType t = CompressionType::Snappy, int level = 0);

    // Uncompresses [input, input + length) into *output.
    bool uncompress(const char *input, size_t length, std::string *output,
                    CompressionType t = CompressionType::Snappy);

    // Sets *result to the uncompressed size of the data in
    // [input, input + length).
    bool get_uncompressed_length(const char *input, size_t length,
                                 size_t *result,
                                 CompressionType t = CompressionType::Snappy);
};

} // namespace anchordb
