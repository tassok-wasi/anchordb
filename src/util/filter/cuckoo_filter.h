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
#include "anchordb/filter.h"
#include "util/hash.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ANCHORDB_NAMESPACE {

// Shared constants so the free matching function can use them without
// reaching into the class.
inline constexpr size_t kCuckooSlotsPerBucket = 4;
inline constexpr size_t kCuckooFingerprintBits = 8; // 1-byte fingerprint
static constexpr size_t kMaxReallocations = 500;

namespace {

inline uint32_t cuckoo_hash(std::string_view key) {
    return murmurhash2_32(key.data(), key.size(), 0xbd1e2a57);
}

// Returns a non-zero 1-byte fingerprint.  0 is reserved as the empty-slot
// marker.
inline uint8_t cuckoo_fingerprint(uint32_t h) {
    uint8_t fp = static_cast<uint8_t>(h & 0xFF);
    return fp == 0 ? 1 : fp;
}

// num_buckets must be a power of two.  Using masking instead of modulo
// preserves the fundamental cuckoo invariant:
//   alt(alt(i, fp), fp) == i
inline size_t cuckoo_index1(uint32_t h, size_t num_buckets) {
    return (h >> 8) & (num_buckets - 1);
}

inline size_t cuckoo_index2(size_t index1, uint8_t fp, size_t num_buckets) {
    const uint32_t fp_hash = fp * 0x5bd1e995; // MurmurHash2 mixing constant
    return (index1 ^ fp_hash) & (num_buckets - 1);
}

// Deterministic xorshift RNG — replaces rand() for reproducibility and
// thread-safety.
inline uint32_t cuckoo_rand(uint32_t &state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

} // namespace

inline const char *cuckoo_name(const void *state_ptr) {
    return "anchordb.BuiltinCuckooFilter";
}

void cuckoo_create(const void *state_ptr, const std::string_view *keys,
                   size_t n, std::string *dst);

bool cuckoo_match(const void *state_ptr, std::string_view key,
                  std::string_view cuckoo_filter);

inline Filter new_cuckoo_filter() {
    return Filter{nullptr, &cuckoo_name, &cuckoo_create, &cuckoo_match,
                  nullptr};
}

} // namespace ANCHORDB_NAMESPACE
