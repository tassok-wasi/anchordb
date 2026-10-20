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
#include "cache/cache_wrapper.h"
#include "sstable/table.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ANCHORDB_NAMESPACE {

// Caches open Tables so repeated reads of the same table number avoid
// re-opening files and re-parsing the summary.  The backing Cache is chosen by
// Options::table_cache_type/size and owned through a CacheWrapper.
//
// Captured Options are forwarded unchanged to Table::open, so cached tables use
// the same compressor the writer used.  Only the parsed summary is charged to
// the cache; index blocks are decompressed per load.
//
// The cache holds a Table* with one reference (the deleter unrefs it); callers
// of find_table receive their own reference.  Keys are namespaced by a
// per-TableCache id so multiple TableCaches can share one Cache.
class TableCache {
  public:
    TableCache(const Options &options, std::string db_dir);
    ~TableCache();

    TableCache(const TableCache &) = delete;
    TableCache &operator=(const TableCache &) = delete;

    // Point lookup against table `table_number`, opening and caching the table
    // on first use.
    Status get(const ReadOptions &options, uint64_t table_number,
               std::string_view key, std::string *value, std::string *scratch);

    // Returns a heap-allocated iterator over `table_number` (caller deletes).
    // The iterator keeps the cached table alive.
    Status new_iterator(const ReadOptions &options, uint64_t table_number,
                        Iterator **iter);

    // Drops the cached entry for `table_number` (e.g. after compaction).
    Status erase(uint64_t table_number);

    size_t total_charge() const { return cache_.total_charge(); }
    uint64_t namespace_id() const { return namespace_id_; }

  private:
    // Looks up or opens the table, returning a new reference in *out that the
    // caller must unref().
    Status find_table(uint64_t table_number, Table **out);

    // Builds the namespaced cache key for a table number.
    std::string cache_key(uint64_t table_number) const;

    // Cache deleter for a Table* cache value.
    static void delete_entry(std::string_view key, void *value);

    Options options_;
    std::string db_dir_;
    CacheWrapper cache_;
    uint64_t namespace_id_;
};

} // namespace ANCHORDB_NAMESPACE
