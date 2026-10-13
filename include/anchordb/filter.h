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
namespace anchordb {

struct ANCHORDB_EXPORT Filter {
    // We use a void* to hold a pointer to the filter's configuration state,
    const void *state = nullptr;

    const char *(*name)(const void *state) = nullptr;
    void (*create_filter)(const void *state, const std::string_view *keys,
                          size_t n, std::string *dst) = nullptr;
    bool (*key_may_match)(const void *state, std::string_view key,
                          std::string_view filter) = nullptr;

    // Optional destructor function pointer to clean up config state if it was
    // heap-allocated
    void (*destroy)(const void *state) = nullptr;
};

Filter ANCHORDB_EXPORT new_bloom_filter(size_t bits_per_key);
Filter ANCHORDB_EXPORT new_cuckoo_filter();
Filter ANCHORDB_EXPORT new_ribbon_filter(size_t bits_per_key);

} // namespace anchordb
