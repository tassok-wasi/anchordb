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
#include "anchordb/compressor.h"
#include "anchordb/filter.h"
#include "anchordb/status.h"
#include "db/db_format.h"
#include "sstable/filter_block.h"
#include "sstable/idx_block_writer.h"
#include "sstable/idx_summary.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ANCHORDB_NAMESPACE {

// The two independent outputs of IndexTableBuilder, an SSTable's index side:
//   - `index`:   index.bin, a run of index-block segments back-to-back from
//                byte offset 0.  Each is a fixed-size (aligned) block,
//                optionally compressed; IndexBlockReader parses it after any
//                decompression.
//   - `summary`: summary.bin, the sparse top-level index (see
//                SummaryIndexWriter): one entry per block mapping its greatest
//                key to the block's offset within `index` and its on-disk size.
//   - `filter`:  filter.bin, the per-table key filter (see FilterBlockBuilder),
//                empty when no filter is configured.
struct IndexTableContents {
    // index.bin contents.
    std::string index;
    // summary.bin contents (see SummaryIndexReader).
    std::string summary;
    // filter.bin contents (see FilterBlockReader); empty when disabled.
    std::string filter;
    // Number of index blocks in `index`.
    size_t block_count = 0;
    // Total number of entries stored across all blocks.
    size_t entry_count = 0;

    // Total encoded size of index + summary + filter.
    size_t byte_size() const {
        return index.size() + summary.size() + filter.size();
    }
    bool empty() const { return block_count == 0; }
};

// Builds the index side of an SSTable from a sorted stream of index entries.
//
// Entries must be in index order: user key ascending, then timestamp
// descending for equal keys.  add() enforces this and returns a Status.
//
// Entries are buffered into an IndexBlockWriter.  When a block fills it is
// sealed, appended to the index buffer and recorded in the summary; the
// rejected entry starts a new block.  finish() flushes the trailing block and
// returns the index and summary buffers separately.
//
// A supplied compressor is applied to every sealed block; the default (null)
// compressor stores blocks verbatim.
//
// When a filter is supplied, every accepted user key is also fed to a
// FilterBlockBuilder, and finish() returns the serialized filter alongside the
// index and summary; a disabled (null) filter produces an empty filter buffer.
class IndexTableBuilder {
  public:
    // `block_size` is the fixed index-block segment size, aligned exactly like
    // IndexBlockWriter.  A null comparator falls back to bytewise comparison; a
    // null compressor stores blocks uncompressed.  `compression_level` is
    // passed straight to the compressor.  A null filter disables the per-table
    // filter output.
    explicit IndexTableBuilder(size_t block_size,
                               const Comparator &comparator = Comparator{},
                               Compressor compressor = Compressor{},
                               int compression_level = 0,
                               Filter filter = Filter{});

    IndexTableBuilder(const IndexTableBuilder &) = delete;
    IndexTableBuilder &operator=(const IndexTableBuilder &) = delete;

    // The (aligned) index-block segment size this builder was created with.
    size_t block_size() const { return block_writer_.block_size(); }

    // Appends one index entry.  Returns Status::ok() on success, or
    // Status::invalid_argument if the key/timestamp ordering is violated, the
    // entry is too large to encode, or the builder has already finished.
    Status add(std::string_view key, const ValueHandle &handle,
               uint64_t timestamp);

    // Convenience overload taking the handle fields directly.  `data_size` is
    // the total encoded layout size of the referenced data record.
    Status add(std::string_view key, ValueType type, ValueFlag flag,
               uint64_t data_offset, uint32_t data_size, uint64_t timestamp);

    // Number of blocks already sealed and appended to the index buffer.
    size_t block_count() const { return block_count_; }
    // Total number of entries accepted by add() so far.
    size_t entry_count() const { return entry_count_; }
    bool empty() const { return entry_count_ == 0; }
    bool finished() const { return finished_; }

    // Upper bound on the bytes buffered so far (sealed blocks + the current
    // partial block + the summary).  Useful for pre-reserving buffers.
    size_t size_estimate() const;

    // Flushes the trailing partial block and fills *out with the independent
    // index and summary buffers.  Idempotent; returns invalid_argument if `out`
    // is null.
    Status finish(IndexTableContents *out);

  private:
    // Seals the current (non-empty) block, records it in the summary and
    // appends it to index_.  Must only be called when the block is non-empty.
    Status emit_block();

    Comparator comparator_;
    Compressor compressor_;
    int compression_level_ = 0;
    IndexBlockWriter block_writer_;
    std::string index_;
    SummaryIndexWriter summary_;
    // Collects the accepted user keys for the per-table filter.
    FilterBlockBuilder filter_builder_;
    // Scratch buffer holding the compressed form of the block being emitted.
    std::string compressed_block_;
    // Last accepted key/timestamp, used to enforce ordering.  last_key_ is only
    // meaningful once entry_count_ > 0 (an empty key is a legal key).
    std::string last_key_;
    uint64_t last_timestamp_ = 0;
    size_t block_count_ = 0;
    size_t entry_count_ = 0;
    bool finished_ = false;
};

} // namespace ANCHORDB_NAMESPACE
