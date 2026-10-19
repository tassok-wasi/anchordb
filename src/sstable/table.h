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
#include "anchordb/iterator.h"
#include "anchordb/options.h"
#include "anchordb/status.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ANCHORDB_NAMESPACE {

// The three files that make up one SSTable's readable representation.  Names
// are produced by the shared file-naming helpers in file/filename.h.
struct TableFileSet {
    std::string data;
    std::string index;
    std::string summary;
};

// Builds the .data / .index / .summary file names for `table_number` under
// `db_dir`.
TableFileSet make_table_file_set(std::string_view db_dir,
                                 uint64_t table_number);

class TableIterator;

// Read-only view over one SSTable's index side.  A Table owns the parsed
// summary (loaded fully into memory), random-access readers for the index and
// data files, and the metadata needed to resolve a key to its data record.
//
// Tables are created through open() and shared through manual reference
// counting (ref()/unref(), mirroring MemTable).  A Table starts with one
// reference held by open()'s caller; unref() deletes it at zero.  Once open a
// Table is immutable and safe for concurrent readers.  Each get()/iterator must
// use its own scratch buffers.
//
// All SSTable-specific state (summary reader, file readers, block accessors)
// lives in the private Rep so this header stays free of the on-disk formats.
class Table {
  public:
    Table(const Table &) = delete;
    Table &operator=(const Table &) = delete;

    // Opens the table described by `files`.  `options.env` and
    // `options.comparator` are used.  On success stores a Table with one
    // reference in *result; the caller must eventually unref() it.  On failure
    // returns a non-ok Status and leaves *result untouched.
    static Status open(const Options &options, const TableFileSet &files,
                       Table **result);

    void ref() { ++refs_; }
    void unref();

    // Charge to use when caching this table (resident summary + fixed
    // overhead).
    size_t cache_charge() const;

    // Point lookup.  On success copies the value bytes into *value and returns
    // Status::ok().  Returns Status::not_found if the key is absent or the
    // newest record is a tombstone.  `scratch` is reusable per-call storage for
    // the raw data record.
    Status get(const ReadOptions &options, std::string_view key,
               std::string *value, std::string *scratch) const;

    // Returns a heap-allocated iterator over the whole table (caller deletes).
    // An empty table yields an empty iterator; a malformed table yields an
    // error iterator.  The iterator holds its own reference to the table.
    Iterator *new_iterator(const ReadOptions &options);

  private:
    struct Rep;
    friend class TableIterator;

    explicit Table(Rep *rep) : rep_(rep) {}
    ~Table();

    Rep *rep_;
    int refs_ = 0;
};

} // namespace ANCHORDB_NAMESPACE
