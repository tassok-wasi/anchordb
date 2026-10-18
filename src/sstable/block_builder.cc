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

// BlockBuilder generates blocks where keys are prefix-compressed:
//
// An entry for a particular key-value pair has the form:
//     shared_bytes: varint32
//     unshared_bytes: varint32
//     value_length: varint32
//     key_delta: char[unshared_bytes]
//     value: char[value_length]
// shared_bytes == 0 for restart points.
//
// The trailer of the block has the form:
//     restarts: uint32[num_restarts]
//     num_restarts: uint32
// restarts[i] contains the offset within the block of the ith restart point.

#include "anchordb/options.h"
#include "sstable/block_builder.h"
#include "util/coding.h"
#include <cassert>
#include <cstddef>
#include <string_view>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

BlockBuilder::BlockBuilder(const Options *options)
    : options_(options), restarts_(), counter_(0), finished_(false) {
    assert(options_->block_restart_interval >= 1);
    restarts_.push_back(0); // Fist restart point is at offset 0
}

void BlockBuilder::reset() {
    buffer_.clear();
    restarts_.clear();
    restarts_.push_back(0); // First restart point is at offset 0
    counter_ = 0;
    finished_ = false;
    last_key_.clear();
}

size_t BlockBuilder::current_size_estimate() const {
    return (buffer_.size() +                      // Raw data buffer
            restarts_.size() * sizeof(uint32_t) + // Restart array
            sizeof(uint32_t));                    // Restart array length
}

std::string_view BlockBuilder::finish() {
    size_t restarts_size = restarts_.size();
    for (size_t i = 0; i < restarts_size; ++i) {
        put_fixed_32(&buffer_, restarts_[i]);
    }
    put_fixed_32(&buffer_, restarts_size);
    finished_ = true;
    return std::string_view(buffer_);
}

void BlockBuilder::add(std::string_view key, std::string_view value) {
    std::string_view last_key_piece(last_key_);
    assert(!finished_);
    assert(counter_ <= options_->block_restart_interval);
    assert(buffer_.empty() // No values yet?
           || options_->comparator.compare(key, last_key_piece) > 0);
    size_t shared = 0;
    if (counter_ < options_->block_restart_interval) {
        // See how much sharing to do with previous string
        const size_t min_length = std::min(last_key_piece.size(), key.size());
        while ((shared < min_length) &&
               (last_key_piece[shared] == key[shared])) {
            shared++;
        }
    } else {
        // Restart compression
        restarts_.push_back(buffer_.size());
        counter_ = 0;
    }
    const size_t non_shared = key.size() - shared;

    // Add "<shared><non_shared><value_size>" to buffer_
    put_varint_32(&buffer_, shared);
    put_varint_32(&buffer_, non_shared);
    put_varint_32(&buffer_, value.size());

    // Add string delta to buffer_ followed by value
    buffer_.append(key.data() + shared, non_shared);
    buffer_.append(value.data(), value.size());

    // Update state
    last_key_.resize(shared);
    last_key_.append(key.data() + shared, non_shared);
    assert(std::string_view(last_key_) == key);
    counter_++;
}

} // namespace anchordb
