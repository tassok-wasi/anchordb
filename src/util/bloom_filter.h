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
#include "hash.h"
#include <cstdint>
#include <string>
namespace anchordb {

namespace {
static uint32_t bloom_hash(const Slice &key) {
    return hash(key.data(), key.size(), 0xbc9f1d34);
}
} // namespace

class BloomFilter : public Filter {
  public:
    explicit BloomFilter(int bits_per_key) : bits_per_key_(bits_per_key) {
        k_ = static_cast<size_t>(bits_per_key * 0.69); // 0.69 =~ ln(2)
        if (k_ < 1)
            k_ = 1;
        if (k_ > 30)
            k_ = 30;
    }

    const char *name() const override { return "anchordb.BuiltinBloomFilter"; }

    const FilterType get_type() const override {
        return FilterType::BuiltinBloom;
    }

    void create_filter(const Slice *keys, int n,
                       std::string *dst) const override;

  private:
    size_t bits_per_key_;
    size_t k_;
};

// for builtin filter types we implement the key matching logic outside of the
// class to avoid vtable overhead in the key matching loop
bool bloom_key_may_match(const Slice &key, const Slice &bloom_filter);

} // namespace anchordb
