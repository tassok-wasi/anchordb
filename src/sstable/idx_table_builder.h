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
#include "anchordb/status.h"
#include "db/db_format.h"
#include "sstable/idx_block_writer.h"
#include "sstable/idx_summary.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ANCHORDB_NAMESPACE {

// The two independent outputs of IndexTableBuilder.
//
// An SSTable's index side is split into two files:
//   - `index`:   index.bin, a run of fixed-size (kDefaultBlockSize = 64KB)
//   index
//                blocks laid out back-to-back starting at byte offset 0.  This
//                is what IndexBlockReader parses.
//   - `summary`: summary.bin, the sparse top-level index built by
//                SummaryIndexWriter: one entry per block holding that block's
//                greatest key and the block's absolute byte offset *within*
//                `index`.  Loading it yields the exact block offset to read
//                from index.bin for a given key.
//
// The two buffers are written to two separate files by the caller; the summary
// offset space is relative to the start of `index`.
struct IndexTableContents {
    // index.bin contents.
    std::string index;
    // summary.bin contents (see SummaryIndexReader).
    std::string summary;
    // Number of 64KB index blocks in `index`.
    size_t block_count = 0;
    // Total number of entries stored across all blocks.
    size_t entry_count = 0;

    // Total encoded size of index + summary.
    size_t byte_size() const { return index.size() + summary.size(); }
    bool empty() const { return block_count == 0; }
};

// Builds the index side of an SSTable from a sorted stream of index entries.
//
// Entries must be supplied in the ordering used by the SSTable index: user key
// ascending, and, for equal keys, timestamp descending (newest first).  add()
// enforces this and reports a Status rather than silently producing an
// unsorted (unsearchable) index.
//
// The builder buffers entries into an IndexBlockWriter.  Whenever a block
// fills, it is sealed, appended to the index buffer, and recorded in the
// summary index; a new block is started for the rejected entry.  finish()
// flushes the trailing partial block and returns the index and summary buffers
// separately so they can be written to their own files.
class IndexTableBuilder {
  public:
    // `comparator` defines key ordering; a null/default comparator falls back
    // to bytewise comparison.
    explicit IndexTableBuilder(const Comparator &comparator = Comparator{});

    IndexTableBuilder(const IndexTableBuilder &) = delete;
    IndexTableBuilder &operator=(const IndexTableBuilder &) = delete;

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

    // Flushes the trailing partial block, builds the summary index and fills
    // *out.  `out->index` and `out->summary` are independent buffers, ready to
    // be written to the index and summary files respectively.  Idempotent:
    // calling it again returns the same contents.  Returns
    // Status::invalid_argument if `out` is null.
    Status finish(IndexTableContents *out);

  private:
    // Seals the current (non-empty) block, records it in the summary and
    // appends it to index_.  Must only be called when the block is non-empty.
    Status emit_block();

    Comparator comparator_;
    IndexBlockWriter block_writer_;
    std::string index_;
    SummaryIndexWriter summary_;
    // Last accepted key/timestamp, used to enforce ordering.  last_key_ is only
    // meaningful once entry_count_ > 0 (an empty key is a legal key).
    std::string last_key_;
    uint64_t last_timestamp_ = 0;
    size_t block_count_ = 0;
    size_t entry_count_ = 0;
    bool finished_ = false;
};

} // namespace ANCHORDB_NAMESPACE