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
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ANCHORDB_NAMESPACE {

// Size of the summary header: the index-block segment size the table was built
// with.  Written once at the very start of summary.bin.
inline constexpr size_t kSummaryBlockSizeSize = sizeof(uint32_t); // 4 bytes

// Fixed on-disk sizes of a summary index entry's non-key fields.
inline constexpr size_t kSummaryKeyLenSize = sizeof(uint32_t); // 4 bytes
inline constexpr size_t kSummaryOffsetSize = sizeof(uint64_t); // 8 bytes
inline constexpr size_t kSummaryCompressedSizeSize =
    sizeof(uint32_t); // 4 bytes

// One decoded summary index entry.  `key` is a view into the buffer the reader
// was constructed from and must not outlive it.
struct SummaryEntry {
    std::string_view key;
    uint64_t block_offset;
    // On-disk (compressed) byte length of the index block starting at
    // block_offset; used to read back exactly the block that was written.
    uint32_t compressed_size;
};

// Builds the summary (top-level) index: one entry per index block, holding the
// block's greatest key, its absolute byte offset and its on-disk (compressed)
// size.
//
// Buffer encoding (little-endian):
//   | block_size (4B) | entry* |
// where each entry is:
//   | key_len (4B) | key_bytes (key_len B) | block_offset (8B) |
//   | compressed_size (4B) |
//
// The leading block_size header records the index-block segment size for the
// whole table.  Entries must be added in ascending key order.
class SummaryIndexWriter {
  public:
    // `block_size` is the index-block segment size for the whole table and is
    // written as the summary header.
    explicit SummaryIndexWriter(uint32_t block_size);

    SummaryIndexWriter(const SummaryIndexWriter &) = delete;
    SummaryIndexWriter &operator=(const SummaryIndexWriter &) = delete;

    void reset() {
        buffer_.clear();
        entry_count_ = 0;
        write_header_();
    }

    // Index-block segment size this summary was built for.
    uint32_t block_size() const { return block_size_; }

    // Appends one (last_key, absolute_block_offset, compressed_size) triple.
    // `compressed_size` is the on-disk byte length of the block at
    // block_offset.  Returns false (and writes nothing) if the key is too long
    // to encode in 32 bits.
    bool add(std::string_view key, uint64_t block_offset,
             uint32_t compressed_size);

    // Returns the encoded summary buffer.  Valid until the next mutation.
    std::string_view finish() const { return buffer_; }

    // Number of entries appended so far.
    size_t size() const { return entry_count_; }
    bool empty() const { return entry_count_ == 0; }
    // Total encoded byte length of the buffer (header + entries).
    size_t byte_size() const { return buffer_.size(); }

  private:
    // Appends the leading block_size header that starts the buffer.
    void write_header_();

    uint32_t block_size_ = 0;
    std::string buffer_;
    size_t entry_count_ = 0;
};

// Parses a summary index buffer and resolves lookups to absolute block offsets.
// Views stored by the reader point into the supplied buffer, which must outlive
// it.  Ordering uses the supplied Comparator (null falls back to bytewise).
class SummaryIndexReader {
  public:
    explicit SummaryIndexReader(std::string_view buffer,
                                Comparator comparator = Comparator{});

    SummaryIndexReader(const SummaryIndexReader &) = delete;
    SummaryIndexReader &operator=(const SummaryIndexReader &) = delete;

    // False if the buffer was truncated or otherwise malformed while parsing.
    bool valid() const { return valid_; }

    // Index-block segment size recorded in the summary header.  Only meaningful
    // when valid() is true; zero otherwise.
    uint32_t block_size() const { return block_size_; }

    // Finds the block that may contain `target`: the first entry whose key is
    // >= target.  On success stores its absolute block offset and returns true;
    // returns false if the buffer is invalid or target is past the last block.
    bool seek(std::string_view target, uint64_t *block_offset) const;

    // As above, also reporting the block's on-disk (compressed) size.  Either
    // output pointer may be null.
    bool seek(std::string_view target, uint64_t *block_offset,
              uint32_t *compressed_size) const;

    // Index of the first entry whose key is >= target, or size() when every
    // stored key is smaller (or the buffer is invalid).
    size_t lower_bound(std::string_view target) const;

    size_t size() const { return entries_.size(); }
    bool empty() const { return entries_.empty(); }

    const std::vector<SummaryEntry> &entries() const { return entries_; }
    const SummaryEntry &entry(size_t index) const { return entries_[index]; }

  private:
    Comparator comparator_;
    std::vector<SummaryEntry> entries_;
    uint32_t block_size_ = 0;
    bool valid_;
};

} // namespace ANCHORDB_NAMESPACE
