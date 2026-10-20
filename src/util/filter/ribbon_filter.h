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

// Ribbon-filter constants shared with the free matching function.
inline constexpr size_t k_ribbon_width = 64; // coefficient width in bits
inline constexpr size_t k_ribbon_fingerprint_bits = 8; // FPR ≈ 2⁻⁸ (≈ 0.4 %)

namespace {

// Returns a 64-bit ribbon coefficient with the LSB forced to 1 so the
// coefficient is invertible over GF(2) — a requirement for the banded
// Gaussian elimination to succeed.
inline uint64_t ribbon_mask(std::string_view key) {
    const uint32_t h1 = murmurhash2_32(key.data(), key.size(), 0x9e3779b9);
    const uint32_t h2 = murmurhash2_32(key.data(), key.size(), 0x85ebca6b);
    const uint64_t mask = (static_cast<uint64_t>(h1) << 32) | h2;
    return mask | 1ULL;
}

// Returns a non-zero 8-bit fingerprint.  The false-positive rate is 2⁻⁸.
inline uint8_t ribbon_fingerprint(std::string_view key) {
    const uint32_t h = murmurhash2_32(key.data(), key.size(), 0x47b6137b);
    uint8_t fp = static_cast<uint8_t>(h & 0xFF);
    return fp == 0 ? 1 : fp;
}

// Maps a key to a starting column in [0, num_bits - k_ribbon_width].
// Precondition: num_bits > k_ribbon_width.
inline size_t ribbon_start(std::string_view key, size_t num_bits) {
    const uint32_t h = murmurhash2_32(key.data(), key.size(), 0x47b6137b);
    return static_cast<size_t>(h % (num_bits - k_ribbon_width));
}

} // namespace

struct RibbonState {
    size_t bits_per_key;
};

inline const char *ribbon_name(const void *state_ptr) {
    return "anchordb.BuiltinRibbonFilter";
}

void ribbon_create(const void *state_ptr, const std::string_view *keys,
                   size_t n, std::string *dst);

bool ribbon_match(const void *state_ptr, std::string_view key,
                  std::string_view ribbon_filter);

inline void ribbon_destroy(const void *state_ptr) {
    delete static_cast<const RibbonState *>(state_ptr);
}

inline Filter make_ribbon_filter(size_t bits_per_key) {
    size_t bits_per_key_ = (bits_per_key >= 2) ? bits_per_key : 10;
    auto *state = new RibbonState{bits_per_key_};
    return Filter{state, &ribbon_name, &ribbon_create, &ribbon_match,
                  &ribbon_destroy};
}

} // namespace ANCHORDB_NAMESPACE
