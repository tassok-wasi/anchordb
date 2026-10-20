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

#include "anchordb/filter.h"

#include "anchordb/anchordb_namespace.h"
#include "bloom_filter.h"
#include "cuckoo_filter.h"
#include "ribbon_filter.h"

namespace ANCHORDB_NAMESPACE {

namespace {
// Bits per key used when new_filter() materializes a sized filter.
constexpr size_t kDefaultBitsPerKey = 10;
} // namespace

Filter new_bloom_filter(size_t bits_per_key) {
    return make_bloom_filter(bits_per_key);
}

Filter new_cuckoo_filter() { return make_cuckoo_filter(); }

Filter new_ribbon_filter(size_t bits_per_key) {
    return make_ribbon_filter(bits_per_key);
}

Filter new_filter(FilterType type) {
    switch (type) {
    case FilterType::Cuckoo: return new_cuckoo_filter();
    case FilterType::Ribbon: return new_ribbon_filter(kDefaultBitsPerKey);
    case FilterType::Bloom:
    default: return new_bloom_filter(kDefaultBitsPerKey);
    }
}

} // namespace ANCHORDB_NAMESPACE
