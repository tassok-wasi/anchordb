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

#include "anchordb/anchordb_namespace.h"
#include "anchordb/comparator.h"
#include "anchordb/iterator.h"
#include "sstable/idx_block_writer.h"
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ANCHORDB_NAMESPACE {

// Encoded size of the value returned by an index block iterator's value():
//   [ value_type (1B) | value_flag (1B) | data_size (4B) | offset (8B) |
//     timestamp (8B) ]
// Decode it with IndexBlockReader::decode_value().
inline constexpr size_t kIndexValueSize = sizeof(uint8_t) + sizeof(uint8_t) +
                                          sizeof(uint32_t) + sizeof(uint64_t) +
                                          sizeof(uint64_t);

// A fully decoded index block entry.
struct IndexBlockEntry {
    std::string_view key;
    ValueHandle handle;
    uint64_t timestamp;
};

// Parses an index block segment produced by IndexBlockWriter and exposes its
// entries.  All returned std::string_views point into the block buffer, which
// must outlive this reader.
//
// The block is validated on construction; valid() is false if it is truncated
// or otherwise malformed.
class IndexBlockReader {
  public:
    explicit IndexBlockReader(std::string_view block);

    IndexBlockReader(const IndexBlockReader &) = delete;
    IndexBlockReader &operator=(const IndexBlockReader &) = delete;

    bool valid() const { return valid_; }

    // Number of entries (slots) in the block.
    size_t size() const { return slot_count_; }
    bool empty() const { return slot_count_ == 0; }

    // Decoded entry at index (0 <= index < size()).  Slot order is ascending
    // key order.
    IndexBlockEntry entry(size_t index) const;
    std::string_view key(size_t index) const { return entry(index).key; }
    ValueHandle handle(size_t index) const { return entry(index).handle; }
    uint64_t timestamp(size_t index) const { return entry(index).timestamp; }

    // Returns a heap-allocated iterator over the entries in ascending key
    // order: key() is the entry key and value() is a kIndexValueSize-byte
    // record (see decode_value).  The caller owns the returned iterator and
    // must keep this reader (and its block buffer) alive for its lifetime.
    // A malformed block yields an error iterator; an empty block yields an
    // empty iterator.
    Iterator *new_iterator(Comparator comparator = Comparator{}) const;

    // Decodes a value() view produced by the iterator into a handle/timestamp.
    // Returns false if the view is not exactly kIndexValueSize bytes.
    static bool decode_value(std::string_view value, ValueHandle *handle,
                             uint64_t *timestamp);

  private:
    const char *data_;
    size_t size_;
    uint16_t slot_count_;
    bool valid_;
};

} // namespace ANCHORDB_NAMESPACE
