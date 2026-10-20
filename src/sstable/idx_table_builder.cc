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

#include "sstable/idx_table_builder.h"
#include "anchordb/status.h"
#include "sstable/idx_block_writer.h"
#include "sstable/idx_summary.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

IndexTableBuilder::IndexTableBuilder(size_t block_size,
                                     const Comparator &comparator,
                                     Compressor compressor,
                                     int compression_level, Filter filter)
    : comparator_(comparator), compressor_(compressor),
      compression_level_(compression_level), block_writer_(block_size),
      summary_(static_cast<uint32_t>(block_writer_.block_size())),
      filter_builder_(filter) {
    if (comparator_.compare == nullptr) {
        comparator_ = new_byte_wise_comparator();
    }
}

Status IndexTableBuilder::add(std::string_view key, const ValueHandle &handle,
                              uint64_t timestamp) {
    if (finished_) {
        return Status::invalid_argument(
            "index table builder: already finished");
    }

    // Reject entries that could never fit in a single (configurable) block, so
    // the retry after a flush below is guaranteed to succeed.
    const size_t payload_size =
        sizeof(uint32_t) + key.size() + sizeof(uint64_t);
    if (key.size() > UINT32_MAX || payload_size > block_writer_.block_size()) {
        return Status::invalid_argument("index table builder: key too large");
    }

    // Enforce the index ordering: user key ascending, then timestamp
    // descending for equal keys.
    if (entry_count_ > 0) {
        const int cmp = comparator_.compare(key, last_key_);
        if (cmp < 0) {
            return Status::invalid_argument(
                "index table builder: keys must be added in ascending order");
        }
        if (cmp == 0 && timestamp > last_timestamp_) {
            return Status::invalid_argument(
                "index table builder: timestamps must decrease for equal keys");
        }
    }

    if (!block_writer_.add(key, handle, timestamp)) {
        // The block is full.  Seal it and retry on a fresh block; the size
        // check above guarantees the entry fits in an empty block.
        assert(!block_writer_.empty());
        Status s = emit_block();
        if (!s.is_ok()) { return s; }
        if (!block_writer_.add(key, handle, timestamp)) {
            return Status::invalid_argument(
                "index table builder: entry too large for index block");
        }
    }

    last_key_.assign(key);
    last_timestamp_ = timestamp;
    ++entry_count_;
    filter_builder_.add_key(key);
    return Status::ok();
}

Status IndexTableBuilder::add(std::string_view key, ValueType type,
                              ValueFlag flag, uint64_t data_offset,
                              uint32_t data_size, uint64_t timestamp) {
    return add(key, ValueHandle{type, flag, data_offset, data_size}, timestamp);
}

size_t IndexTableBuilder::size_estimate() const {
    return index_.size() + summary_.byte_size() +
           (block_writer_.empty() ? 0 : block_writer_.block_size());
}

Status IndexTableBuilder::emit_block() {
    assert(!block_writer_.empty());

    const uint64_t block_offset = index_.size();
    const std::string_view block = block_writer_.finish();
    assert(block.size() == block_writer_.block_size());

    // Compress when configured; the summary records the stored (compressed)
    // length so the reader can fetch exactly this block back.
    std::string_view stored = block;
    if (compressor_.compress != nullptr) {
        if (!compressor_.compress(block.data(), block.size(),
                                  &compressed_block_, compression_level_)) {
            return Status::io_error(std::string_view(
                "index table builder: index block compression failed"));
        }
        stored = compressed_block_;
    }
    if (stored.size() > UINT32_MAX) {
        return Status::invalid_argument(
            "index table builder: stored index block too large");
    }

    // Record the block in the summary before the writer is reset (last_key()
    // is a view into the writer's buffer).
    if (!summary_.add(block_writer_.last_key(), block_offset,
                      static_cast<uint32_t>(stored.size()))) {
        return Status::invalid_argument(
            "index table builder: separator key too large for summary index");
    }

    index_.append(stored.data(), stored.size());
    ++block_count_;
    block_writer_.reset();
    return Status::ok();
}

Status IndexTableBuilder::finish(IndexTableContents *out) {
    if (out == nullptr) {
        return Status::invalid_argument("index table builder: null output");
    }

    if (!finished_) {
        if (!block_writer_.empty()) {
            Status s = emit_block();
            if (!s.is_ok()) { return s; }
        }
        finished_ = true;
    }

    out->index = index_;
    const std::string_view summary = summary_.finish();
    out->summary.assign(summary.data(), summary.size());
    out->filter = filter_builder_.finish();
    out->block_count = block_count_;
    out->entry_count = entry_count_;
    return Status::ok();
}

} // namespace ANCHORDB_NAMESPACE
