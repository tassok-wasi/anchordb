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
#include "anchordb/env.h"
#include "anchordb/iterator.h"
#include "anchordb/options.h"
#include "anchordb/status.h"
#include "sstable/idx_block_reader.h"
#include "sstable/idx_block_writer.h"
#include "sstable/idx_summary.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace ANCHORDB_NAMESPACE {

// Fixed byte overhead charged to the table cache per open table, on top of the
// resident summary bytes.
inline constexpr size_t kTableCacheEntryOverhead = 256;

// Upper bound on a summary file we are willing to load into memory.
inline constexpr uint64_t kMaxSummaryFileSize = 64ull * 1024 * 1024;

// The three files that make up one SSTable's readable representation.
enum class TableFilePart : uint8_t {
    data = 0x0,
    index = 0x1,
    summary = 0x2,
};

struct TableFileSet {
    std::string data;
    std::string index;
    std::string summary;
};

// Builds the file name for a table part: "<dir>/<number zero-padded to 6>
// <suffix>", where suffix is ".data" / ".index" / ".summary".
std::string table_file_name(std::string_view dir, uint64_t table_number,
                            TableFilePart part);
TableFileSet make_table_file_set(std::string_view dir, uint64_t table_number);

// Read-only view over one SSTable's index side.  A Table owns the parsed
// summary (loaded fully into memory), random-access readers for the index and
// data files, and the metadata needed to resolve a key to its data record.
//
// Tables are created through open() and shared via std::shared_ptr; once open
// a Table is immutable and safe for concurrent readers.  Each get()/iterator
// must use its own scratch buffers.
class Table : public std::enable_shared_from_this<Table> {
  public:
    Table(const Table &) = delete;
    Table &operator=(const Table &) = delete;

    // Opens the table described by `files`.  `options.env` and
    // `options.comparator` are used.  On success stores a shared Table in
    // *result; on failure returns a non-ok Status and leaves *result untouched.
    static Status open(const Options &options, const TableFileSet &files,
                       std::shared_ptr<Table> *result);

    const Comparator &comparator() const { return comparator_; }
    uint64_t index_file_size() const { return index_file_size_; }
    uint64_t data_file_size() const { return data_file_size_; }
    size_t block_count() const { return block_count_; }

    // Charge to use when caching this table (resident summary + fixed
    // overhead).
    size_t cache_charge() const {
        return summary_buffer_.size() + kTableCacheEntryOverhead;
    }

    // Point lookup.  On success copies the value bytes into *value and returns
    // Status::ok().  Returns Status::not_found if the key is absent or the
    // newest record is a tombstone.  `scratch` is reusable per-call storage for
    // the raw data record.
    Status get(const ReadOptions &options, std::string_view key,
               std::string *value, std::string *scratch) const;

    // Returns a heap-allocated iterator over the whole table (caller deletes).
    // An empty table yields an empty iterator; a malformed table yields an
    // error iterator.
    Iterator *new_iterator(const ReadOptions &options) const;

  private:
    friend class TableIterator;

    Table() = default;

    // Reads `n` bytes at `offset` from `reader` into *buf and points *out at
    // the bytes read.  Fails if fewer than `n` bytes are available.
    static Status read_exact(RandomAccessFileReader *reader, uint64_t offset,
                             size_t n, std::string *buf, std::string_view *out);

    // Reads the full 64KB index block starting at `block_offset`.
    Status read_index_block(uint64_t block_offset, std::string *buf,
                            std::string_view *out) const;

    // Reads and validates the data record referenced by `handle`, cross-
    // checking it against `expected_key` and the handle metadata.  Returns
    // Status::not_found for tombstones, Status::not_supported for blob values.
    Status read_record(const ValueHandle &handle, std::string_view expected_key,
                       std::string *value, std::string *scratch) const;

    std::string summary_buffer_;
    std::unique_ptr<SummaryIndexReader> summary_;
    std::unique_ptr<RandomAccessFileReader> index_reader_;
    std::unique_ptr<RandomAccessFileReader> data_reader_;
    uint64_t index_file_size_ = 0;
    uint64_t data_file_size_ = 0;
    size_t block_count_ = 0;
    Comparator comparator_;
};

// Bidirectional iterator over a Table, keeping one 64KB index block resident
// at a time and transparently spanning block boundaries.  Tombstone entries are
// filtered out; blob (vLog) values surface an error via status().
class TableIterator : public Iterator {
  public:
    TableIterator(std::shared_ptr<const Table> table, ReadOptions options);
    ~TableIterator() override;

    TableIterator(const TableIterator &) = delete;
    TableIterator &operator=(const TableIterator &) = delete;

    bool valid() const override;
    void seek_to_first() override;
    void seek_to_last() override;
    void seek(std::string_view target) override;
    void next() override;
    void prev() override;
    std::string_view key() const override;
    std::string_view value() const override;
    Status status() const override;

  private:
    // Loads the 64KB block at `block_index` into block_buf_ and rebuilds
    // reader_.  Sets status_ and invalidates on failure.
    void load_block(size_t block_index);

    // Fills key_/value_ from the current slot via the table.  Sets status_ and
    // invalidates on failure.
    void materialize_current();

    bool visible(size_t slot) const;

    // Moves forward from the current slot to the next visible entry, crossing
    // blocks as needed.  Invalidates when exhausted.
    void skip_forward();
    // Moves backward from the current slot to the previous visible entry,
    // crossing blocks as needed.  Invalidates when exhausted.
    void skip_backward();

    void invalidate();

    std::shared_ptr<const Table> table_;
    ReadOptions options_;
    Comparator comparator_;
    size_t block_count_;

    size_t block_index_ = 0;
    std::unique_ptr<IndexBlockReader> reader_;
    std::string block_buf_;

    size_t slot_ = 0;
    bool valid_ = false;

    std::string value_buf_;
    std::string scratch_;
    std::string_view key_;
    std::string_view value_;
    Status status_;
};

} // namespace ANCHORDB_NAMESPACE
