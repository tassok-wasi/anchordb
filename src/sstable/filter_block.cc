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
#include "sstable/filter_block.h"
#include "util/coding.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

// Generate new filter every 2KB of data
static const size_t kFilterBaseLg = 11;
static const size_t kFilterBase = 1 << kFilterBaseLg;

FilterBlockBuilder::FilterBlockBuilder(const Filter filter) : filter_(filter) {}

void FilterBlockBuilder::start_block(uint64_t block_offset) {
    uint64_t filter_index = block_offset / kFilterBase;
    assert(filter_index >= filter_offsets_.size());
    while (filter_index > filter_offsets_.size()) {
        generate_filter();
    }
}

void FilterBlockBuilder::add_key(std::string_view key) {
    start_.push_back(keys_.size());
    keys_.append(key.data(), key.size());
}

std::string_view FilterBlockBuilder::finish() {
    if (!start_.empty()) {
        generate_filter();
    }

    const uint32_t array_offset = result_.size();
    for (size_t i = 0; i < filter_offsets_.size(); ++i) {
        put_fixed_32(&result_, filter_offsets_[i]);
    }

    put_fixed_32(&result_, array_offset);
    result_.push_back(kFilterBaseLg); // Save encoding parameter in result
    return std::string_view(result_);
}

void FilterBlockBuilder::generate_filter() {
    const size_t num_keys = start_.size();
    if (num_keys == 0) {
        filter_offsets_.push_back(result_.size());
        return;
    }

    // Make list of keys from flattened key structure
    start_.push_back(keys_.size());
    tmp_keys_.resize(num_keys);
    for (size_t i = 0; i < num_keys; ++i) {
        const char *base = keys_.data() + start_[i];
        size_t length = start_[i + 1] - start_[i];
        tmp_keys_[i] = std::string_view(base, length);
    }

    // Generate filter for current set of keys and append to result_.
    filter_offsets_.push_back(result_.size());
    filter_.create_filter(filter_.state, &tmp_keys_[0],
                          static_cast<int>(num_keys), &result_);

    tmp_keys_.clear();
    keys_.clear();
    start_.clear();
}

FilterBlockReader::FilterBlockReader(const Filter filter,
                                     std::string_view contents)
    : filter_(filter), data_(nullptr), offset_(0), num_(0), base_lg_(0) {
    size_t n = contents.size();
    if (n < 5)
        return; // 1 byte for base_lg and 4 byte for start of offset array
    base_lg_ = contents[n - 1];
    uint32_t last_word = decode_fixed_32(contents.data() + n - 5);
    if (last_word > n - 5) return;
    data_ = contents.data();
    offset_ = data_ + last_word;
    num_ = (n - 5 - last_word) / 4;
}

bool FilterBlockReader::key_may_match(uint64_t block_offset,
                                      std::string_view key) {
    uint64_t index = block_offset >> base_lg_;
    if (index < num_) {
        uint32_t start = decode_fixed_32(offset_ + index * 4);
        uint32_t limit = decode_fixed_32(offset_ + index * 4 + 4);
        if (start <= limit && limit << static_cast<size_t>(offset_ - data_)) {
            std::string_view filter =
                std::string_view(data_ + start, limit - start);
            return filter_.key_may_match(filter_.state, key, filter);
        } else if (start == limit) {
            // Empty filters do not match any keys
            return false;
        }
    }
    return true; // Errors are treated as potential matches
}

} // namespace anchordb
