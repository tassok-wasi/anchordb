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
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ANCHORDB_NAMESPACE {

// Fixed on-disk sizes of a summary index entry's non-key fields.
inline constexpr size_t kSummaryKeyLenSize = sizeof(uint16_t); // 2 bytes
inline constexpr size_t kSummaryOffsetSize = sizeof(uint64_t); // 8 bytes

// One decoded summary index entry.  `key` is a view into the buffer the reader
// was constructed from and must not outlive it.
struct SummaryEntry {
    std::string_view key;
    uint64_t block_offset;
};

// Builds the summary (top-level) index: one entry per index block, holding that
// block's greatest key and the block's absolute byte offset.
//
// Entry encoding (little-endian):
//   | key_len (2B) | key_bytes (key_len B) | block_offset (8B) |
//
// Entries must be added in ascending key order; the resulting buffer is then
// directly binary-searchable by SummaryIndexReader.
class SummaryIndexWriter {
  public:
    SummaryIndexWriter() = default;

    SummaryIndexWriter(const SummaryIndexWriter &) = delete;
    SummaryIndexWriter &operator=(const SummaryIndexWriter &) = delete;

    void reset() {
        buffer_.clear();
        entry_count_ = 0;
    }

    // Appends one (last_key, absolute_block_offset) pair.  Returns false (and
    // writes nothing) if the key is too long to encode in 16 bits.
    bool add(std::string_view key, uint64_t block_offset);

    // Returns the encoded summary buffer.  Valid until the next mutation.
    std::string_view finish() const { return buffer_; }

    // Number of entries appended so far.
    size_t size() const { return entry_count_; }
    bool empty() const { return entry_count_ == 0; }
    // Total encoded byte length of the buffer.
    size_t byte_size() const { return buffer_.size(); }

  private:
    std::string buffer_;
    size_t entry_count_ = 0;
};

// Parses a summary index buffer and resolves lookups to absolute block offsets.
//
// The reader keeps std::string_view views into the supplied buffer, which must
// therefore outlive the reader.  Ordering is defined by the supplied
// Comparator; a null/default comparator falls back to bytewise comparison.
class SummaryIndexReader {
  public:
    explicit SummaryIndexReader(std::string_view buffer,
                                Comparator comparator = Comparator{});

    SummaryIndexReader(const SummaryIndexReader &) = delete;
    SummaryIndexReader &operator=(const SummaryIndexReader &) = delete;

    // False if the buffer was truncated or otherwise malformed while parsing.
    bool valid() const { return valid_; }

    // Finds the block that may contain `target`: the first entry whose key is
    // >= target.  On success stores that entry's absolute block offset in
    // *block_offset and returns true.  Returns false if the buffer is invalid
    // or target is greater than every stored key (target lies past the last
    // block).
    bool seek(std::string_view target, uint64_t *block_offset) const;

    size_t size() const { return entries_.size(); }
    bool empty() const { return entries_.empty(); }

    const std::vector<SummaryEntry> &entries() const { return entries_; }
    const SummaryEntry &entry(size_t index) const { return entries_[index]; }

    // Returns a heap-allocated iterator over the summary entries in ascending
    // key order.  The iterator's key() is the separator key and value() is the
    // 8-byte little-endian absolute block offset (decode with decode_fixed_64).
    // The caller owns the returned iterator and must keep this reader (and the
    // buffer it was built from) alive for the iterator's lifetime.  If the
    // buffer was malformed, returns an error iterator; if it held no entries,
    // returns an empty iterator.
    Iterator *new_iterator() const;

  private:
    Comparator comparator_;
    std::vector<SummaryEntry> entries_;
    bool valid_;
};

} // namespace ANCHORDB_NAMESPACE
