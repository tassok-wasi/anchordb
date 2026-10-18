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

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

// Extends a running CRC32 checksum with additional data.
// `crc` is the initial CRC value (typically 0 or a previously computed CRC).
// `data` points to the bytes to process, and `n` is the number of bytes.
// Returns the updated CRC32 value.
uint32_t extend(uint32_t crc, const char *data, size_t n);

// Computes the CRC32 checksum of a block of data from scratch.
// Equivalent to calling extend(0, data, n).
uint32_t value(const char *data, size_t n);

// Delta value used by the CRC mask/unmask operations (as in LevelDB).
static const uint32_t k_mask_delta = 0xa282ead8ul; // used by leveldb

// Masks a raw CRC32 value so that it cannot contain a partial frame delimiter.
// The masked value is suitable for storage or transmission.
inline uint32_t mask(uint32_t crc) {
    return ((crc >> 15) | (crc << 17)) + k_mask_delta;
}

// Reverses the mask operation to recover the original CRC32 value.
inline uint32_t unmask(uint32_t masked_crc) {
    uint32_t rot = masked_crc - k_mask_delta;
    return ((rot >> 17) | (rot << 15));
}

} // namespace anchordb
