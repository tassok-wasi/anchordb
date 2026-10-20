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
#include "anchordb/export.h"
#include <cstddef>
#include <string>

namespace ANCHORDB_NAMESPACE {

// Identifies a compression algorithm.  The codec must be available at build
// time; if the corresponding library was not found, new_compressor() returns a
// disabled (pass-through) Compressor for it.
enum class ANCHORDB_EXPORT CompressionType {
    None = 0x0,   // no compression; blocks are stored verbatim
    Snappy = 0x1, // fast, moderate ratio
    Zstd = 0x2,   // tunable level, high ratio
    Lz4 = 0x3,    // very fast, lower ratio
    Zlib = 0x4,   // deflate
};

// Codec interface, expressed as plain function pointers so a custom codec needs
// no inheritance.  A null function pointer means the codec is disabled and
// blocks are stored uncompressed.
struct ANCHORDB_EXPORT Compressor {
    // Compresses `length` bytes at `input` into an existing `*output` string,
    // using `level` as the codec-specific compression level.  Returns false on
    // failure.
    bool (*compress)(const char *input, size_t length, std::string *output,
                     int level) = nullptr;

    // Decompresses `length` bytes at `input` into `output`, which must be large
    // enough for the full uncompressed payload (see get_uncompressed_length).
    // Returns false on failure.
    bool (*uncompress)(const char *input, size_t length,
                       char *output) = nullptr;

    // Reports the uncompressed size of the compressed block at `input`, so the
    // caller can size the destination buffer for uncompress().  Returns false
    // if the input is not a valid compressed block.
    bool (*get_uncompressed_length)(const char *input, size_t length,
                                    size_t *result) = nullptr;
};

Compressor ANCHORDB_EXPORT new_snappy_compressor();
Compressor ANCHORDB_EXPORT new_zstd_compressor();
Compressor ANCHORDB_EXPORT new_lz4_compressor();
Compressor ANCHORDB_EXPORT new_zlib_compressor();

// Returns the built-in compressor for `type`, or a disabled Compressor (null
// function pointers) when `type` has no implementation (e.g.
// CompressionType::None or a codec that was not built).
Compressor ANCHORDB_EXPORT new_compressor(CompressionType type);

} // namespace ANCHORDB_NAMESPACE
