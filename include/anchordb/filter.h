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

#include "anchordb/export.h"
#include <cstddef>
#include <string>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

// Identifies the probabilistic filter algorithm used for SSTable key lookup.
enum class FilterType { Bloom = 0x0, Cuckoo = 0x1, Ribbon = 0x2 };

// Probabilistic membership filter, expressed as plain function pointers so a
// custom filter needs no inheritance.
//
// A filter answers "is this key possibly present?"  It may return false
// positives but never false negatives, so a negative answer lets a reader skip
// a block or table without touching disk.  The filter is built once from the
// block's keys and queried with the serialized filter bytes.
struct ANCHORDB_EXPORT Filter {
    // Opaque pointer to the filter's configuration (e.g. bits-per-key).  It is
    // passed back to every function pointer below and owned by the caller;
    // `destroy` releases it when non-null.
    const void *state = nullptr;

    // Returns a stable, human-readable name for this filter.
    const char *(*name)(const void *state) = nullptr;

    // Builds a filter over the `n` keys in `keys`, appending the serialized
    // filter to `*dst`.
    void (*create_filter)(const void *state, const std::string_view *keys,
                          size_t n, std::string *dst) = nullptr;

    // Returns true if `key` may be in the set the filter was built from, or
    // false if it is definitely absent.
    bool (*key_may_match)(const void *state, std::string_view key,
                          std::string_view filter) = nullptr;

    // Optional; frees the configuration `state` when it was heap-allocated.
    void (*destroy)(const void *state) = nullptr;
};

// Creates a Bloom filter sized to roughly `bits_per_key` bits per key.
Filter ANCHORDB_EXPORT new_bloom_filter(size_t bits_per_key);
// Creates the default Cuckoo filter.
Filter ANCHORDB_EXPORT new_cuckoo_filter();
// Creates a Ribbon filter sized to roughly `bits_per_key` bits per key.
Filter ANCHORDB_EXPORT new_ribbon_filter(size_t bits_per_key);

// Returns the built-in filter for `type`, sized with a default bits-per-key.
// Equivalent to calling the matching new_*_filter factory directly.
Filter ANCHORDB_EXPORT new_filter(FilterType type);

} // namespace ANCHORDB_NAMESPACE
