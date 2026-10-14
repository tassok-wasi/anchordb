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

#include "anchordb/export.h"
#include "anchordb/options.h"
#include <cstddef>
#include <string>
namespace anchordb {

struct ANCHORDB_EXPORT Compressor {
    bool (*compress)(const char *input, size_t length, std::string *output,
                     int level) = nullptr;
    bool (*uncompress)(const char *input, size_t length,
                       char *output) = nullptr;
    bool (*get_uncompressed_length)(const char *input, size_t length,
                                    size_t *result) = nullptr;
};

Compressor ANCHORDB_EXPORT new_snappy_compressor();
Compressor ANCHORDB_EXPORT new_zstd_compressor();
Compressor ANCHORDB_EXPORT new_lz4_compressor();
Compressor ANCHORDB_EXPORT new_zlib_compressor();

// Returns the built-in compressor for `type`. Returns a Compressor with null
// function pointers if `type` has no implementation (e.g.
// CompressionType::None).
Compressor ANCHORDB_EXPORT new_compressor(CompressionType type);

} // namespace anchordb
