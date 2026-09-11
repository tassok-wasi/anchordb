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

#include "anchordb/filter.h"
#include "anchordb/slice.h"
#include "util/hash.h"
#include <cstddef>
#include <cstdint>
#include <string>
namespace anchordb {

// Ribbon-filter constants shared with the free matching function.
inline constexpr size_t k_ribbon_width = 64; // coefficient width in bits
inline constexpr size_t k_ribbon_fingerprint_bits = 8; // FPR ≈ 2⁻⁸ (≈ 0.4 %)

namespace {

// Returns a 64-bit ribbon coefficient with the LSB forced to 1 so the
// coefficient is invertible over GF(2) — a requirement for the banded
// Gaussian elimination to succeed.
inline uint64_t ribbon_mask(const Slice &key) {
    const uint32_t h1 = hash(key.data(), key.size(), 0x9e3779b9);
    const uint32_t h2 = hash(key.data(), key.size(), 0x85ebca6b);
    const uint64_t mask = (static_cast<uint64_t>(h1) << 32) | h2;
    return mask | 1ULL;
}

// Returns a non-zero 8-bit fingerprint.  The false-positive rate is 2⁻⁸.
inline uint8_t ribbon_fingerprint(const Slice &key) {
    const uint32_t h = hash(key.data(), key.size(), 0x47b6137b);
    uint8_t fp = static_cast<uint8_t>(h & 0xFF);
    return fp == 0 ? 1 : fp;
}

// Maps a key to a starting column in [0, num_bits - k_ribbon_width].
// Precondition: num_bits > k_ribbon_width.
inline size_t ribbon_start(const Slice &key, size_t num_bits) {
    const uint32_t h = hash(key.data(), key.size(), 0x47b6137b);
    return static_cast<size_t>(h % (num_bits - k_ribbon_width));
}

} // namespace

// Ribbon filter — a static filter that solves a banded linear system over
// GF(2) to embed 8-bit fingerprints into a compact bit-vector.
//
// The serialized format is:
//   [fingerprint-bits (1 byte)][solution bytes (num_bits bytes)]
// where each solution byte packs the 8 independent solution bits for that
// column (bit j = solution for fingerprint bit j).
class RibbonFilter : public Filter {
  public:
    explicit RibbonFilter(int bits_per_key) {
        // Need at least 2 bits per key for the linear system to be
        // under-determined.
        bits_per_key_ = bits_per_key >= 2 ? static_cast<size_t>(bits_per_key)
                                          : static_cast<size_t>(10);
    }

    const char *name() const override { return "anchordb.BuiltinRibbonFilter"; }

    FilterType get_type() const override {
        return FilterType::BuiltinRibbonFilter;
    }

    void create_filter(const Slice *keys, size_t n,
                       std::string *dst) const override;

  private:
    size_t bits_per_key_;
};

// Matching routine for serialized ribbon filters.
bool ribbon_key_may_match(const Slice &key, const Slice &ribbon_filter);

} // namespace anchordb
