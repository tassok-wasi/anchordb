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
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>
#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

class FilterBlockBuilder {
  public:
    explicit FilterBlockBuilder(const Filter filter);

    FilterBlockBuilder(const FilterBlockBuilder &) = delete;
    FilterBlockBuilder &operator=(const FilterBlockBuilder &) = delete;

    void start_block(uint64_t block_offset);
    void add_key(std::string_view key);
    std::string_view finish();

  private:
    void generate_filter();

    const Filter filter_;
    std::string keys_;          // Flattend key contents
    std::vector<size_t> start_; // Starting index in keys_ of each key
    std::string result_;        // Filter data computed so far
    std::vector<std::string_view> tmp_keys_; // filter_.create_filter() argument
    std::vector<uint32_t> filter_offsets_;
};

class FilterBlockReader {
  public:
    FilterBlockReader(const Filter filter, std::string_view contents);
    bool key_may_match(uint64_t block_offset, std::string_view key);

  private:
    const Filter filter_;
    const char *data_;   // Pointer to filter data (at block-start)
    const char *offset_; // Pointer to beginning of offset array (at block-end)
    size_t num_;         // Number of entries in offset array
    size_t base_lg_;     // Encoding parameter (see kFilterBaseLg in .cc file)
};

} // namespace anchordb
