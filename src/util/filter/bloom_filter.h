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

namespace {
inline uint32_t bloom_hash(const Slice &key) {
    return hash(key.data(), key.size(), 0xbc9f1d34);
}
} // namespace

// Standard Bloom filter.  The number of hash probes `k` is derived from
// `bits_per_key` via the classic k = m/n · ln(2) formula and clamped to
// [1, 30].  The serialized format is:
//   [bit-array (bytes)][k (1 byte)]
class BloomFilter : public Filter {
  public:
    explicit BloomFilter(int bits_per_key) {
        bits_per_key_ = bits_per_key > 0 ? static_cast<size_t>(bits_per_key)
                                         : static_cast<size_t>(10);
        // k = bits_per_key * ln(2)  ≈  bits_per_key * 0.69
        k_ = static_cast<size_t>(static_cast<double>(bits_per_key_) * 0.69);
        if (k_ < 1)
            k_ = 1;
        if (k_ > 30)
            k_ = 30;
    }

    const char *name() const override { return "anchordb.BuiltinBloomFilter"; }

    FilterType get_type() const override {
        return FilterType::BuiltinBloomFilter;
    }

    void create_filter(const Slice *keys, size_t n,
                       std::string *dst) const override;

  private:
    size_t bits_per_key_;
    size_t k_;
};

// Matching routine for serialized bloom filters.  Called by FilterDispatch
// to avoid vtable overhead on the hot query path.
bool bloom_key_may_match(const Slice &key, const Slice &bloom_filter);

} // namespace anchordb
