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

#include "sstable/filter_block.h"
#include "util/coding.h"
#include <cstring>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

FilterBlockBuilder::FilterBlockBuilder(const Filter &filter)
    : filter_(filter),
      enabled_(filter.create_filter != nullptr && filter.name != nullptr) {}

void FilterBlockBuilder::add_key(std::string_view user_key) {
    if (!enabled_) { return; }
    // Keys arrive in ascending order, so equal user keys are adjacent; keeping
    // only the first of each run shrinks the filter without affecting queries.
    if (has_last_key_ && user_key == last_key_) { return; }

    keys_.emplace_back(user_key);
    last_key_.assign(user_key);
    has_last_key_ = true;
}

const std::string &FilterBlockBuilder::finish() {
    if (finished_) { return block_; }
    finished_ = true;
    if (!enabled_) { return block_; }

    block_.clear();
    block_.append(kFilterBlockMagic, sizeof(kFilterBlockMagic));

    const char *name = filter_.name(filter_.state);
    size_t name_len = (name != nullptr) ? std::strlen(name) : 0;
    if (name_len > 255) { name_len = 255; }
    block_.push_back(static_cast<char>(name_len));
    block_.append(name, name_len);

    char count_buf[sizeof(uint32_t)];
    encode_fixed_32(count_buf, static_cast<uint32_t>(keys_.size()));
    block_.append(count_buf, sizeof(count_buf));

    if (!keys_.empty()) {
        std::vector<std::string_view> views(keys_.size());
        for (size_t i = 0; i < keys_.size(); ++i) { views[i] = keys_[i]; }
        filter_.create_filter(filter_.state, views.data(), views.size(),
                              &block_);
    }
    return block_;
}

FilterBlockReader::FilterBlockReader(const Filter &filter,
                                     std::string_view block)
    : filter_(filter) {
    if (filter_.name == nullptr || filter_.key_may_match == nullptr) { return; }
    if (block.size() < kFilterBlockHeaderSize) { return; }
    if (std::memcmp(block.data(), kFilterBlockMagic,
                    sizeof(kFilterBlockMagic)) != 0) {
        return;
    }

    const size_t name_len =
        static_cast<unsigned char>(block[sizeof(kFilterBlockMagic)]);
    const size_t name_start = sizeof(kFilterBlockMagic) + 1;
    if (name_len > block.size() - name_start) { return; }

    const char *expected = filter_.name(filter_.state);
    if (expected == nullptr) { return; }
    if (std::string_view(block.data() + name_start, name_len) !=
        std::string_view(expected)) {
        return;
    }

    const size_t count_offset = name_start + name_len;
    if (block.size() - count_offset < sizeof(uint32_t)) { return; }
    key_count_ = decode_fixed_32(block.data() + count_offset);
    filter_data_ = block.substr(count_offset + sizeof(uint32_t));
    valid_ = true;
}

bool FilterBlockReader::key_may_match(std::string_view user_key) const {
    if (!valid_) { return true; }
    // No keys were stored, so every query is a certain miss.
    if (key_count_ == 0) { return false; }
    // A non-empty table must have filter bytes; if not, stay conservative.
    if (filter_data_.empty()) { return true; }
    return filter_.key_may_match(filter_.state, user_key, filter_data_);
}

} // namespace ANCHORDB_NAMESPACE
