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
#include "util/hash.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

namespace {
inline uint32_t bloom_hash(std::string_view key) {
    return murmurhash2_32(key.data(), key.size(), 0xbc9f1d34);
}
} // namespace

struct BloomFilterState {
    size_t bits_per_key;
    size_t k;
};

inline const char *bloom_name(const void *state_ptr) {
    return "anchordb.BuiltinBloomFilter";
}
void bloom_create(const void *state_ptr, const std::string_view *keys, size_t n,
                  std::string *dst);
bool bloom_match(const void *state_ptr, std::string_view key,
                 std::string_view bloom_filter);
inline void bloom_destroy(const void *state_ptr) {
    delete static_cast<const BloomFilterState *>(state_ptr);
}

inline Filter new_bloom_filter(size_t bits_per_key) {
    size_t bits_per_key_ = (bits_per_key > 0) ? bits_per_key : 10;
    // k = bits_per_key * ln(2)  ≈  bits_per_key * 0.69
    size_t k_ = static_cast<size_t>(static_cast<double>(bits_per_key_) * 0.69);
    if (k_ < 1)
        k_ = 1;
    if (k_ > 30)
        k_ = 30;
    auto *state = new BloomFilterState{bits_per_key_, k_};

    return Filter{&state, &bloom_name, &bloom_create, &bloom_match,
                  &bloom_destroy};
}

} // namespace anchordb
