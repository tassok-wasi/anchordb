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

#include "anchordb/anchordb_namespace.h"
#include "bloom_filter.h"

namespace ANCHORDB_NAMESPACE {

void bloom_create(const void *state_ptr, const std::string_view *keys, size_t n,
                  std::string *dst) {
    if (n == 0) return;

    auto *state = static_cast<const BloomFilterState *>(state_ptr);

    size_t bits = n * state->bits_per_key;
    if (bits < 64) bits = 64;

    const size_t bytes = (bits + 7) / 8;
    bits = bytes * 8; // round up to byte boundary

    // Layout: [bit-array (bytes)][k (1 byte)]
    const size_t init_size = dst->size();
    dst->resize(init_size + bytes + 1, 0);
    char *array = &(*dst)[init_size];
    array[bytes] = static_cast<char>(state->k); // store k in the trailing byte

    for (size_t i = 0; i < n; i++) {
        uint32_t h = bloom_hash(keys[i]);
        const uint32_t delta = (h >> 17) | (h << 15); // double-hashing
        for (size_t j = 0; j < state->k; j++) {
            const size_t bitpos = h % bits;
            array[bitpos >> 3] |= static_cast<char>(1 << (bitpos & 7));
            h += delta;
        }
    }
}

bool bloom_match(const void *state_ptr, std::string_view key,
                 std::string_view bloom_filter) {
    const size_t len = bloom_filter.size();
    if (len < 2) return false;

    const char *array = bloom_filter.data();
    const size_t bits = (len - 1) * 8;
    const size_t k = static_cast<unsigned char>(array[len - 1]);

    // A corrupt k value is handled conservatively: if k is unreasonably large
    // we assume the key *may* match (no false negatives).
    if (k > 30) [[unlikely]]
        return true;

    uint32_t h = bloom_hash(key);
    const uint32_t delta = (h >> 17) | (h << 15);
    for (size_t j = 0; j < k; j++) {
        const size_t bitpos = h % bits;
        if ((array[bitpos >> 3] & (1 << (bitpos & 7))) == 0) [[unlikely]]
            return false;
        h += delta;
    }
    return true;
}

} // namespace ANCHORDB_NAMESPACE
