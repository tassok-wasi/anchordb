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

#include "anchordb/comparator.h"
#include "anchordb/compressor.h"
#include "anchordb/env.h"
#include "anchordb/export.h"
#include "anchordb/filter.h"
#include <cstddef>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

class Cache;

// Selects the open-addressing hash-map backend used for cache indexes (see
// src/cache/).  It affects cache performance, not on-disk format.
enum class ANCHORDB_EXPORT MapType {
    RobbinHood = 0x0,    // Robin Hood probing
    LinearProbing = 0x1, // linear probing
    Swiss = 0x2,         // Swiss-table grouped probing (default)
};

// Selects the replacement policy of the open-table cache (see src/cache/).
enum class ANCHORDB_EXPORT CacheType {
    LRU = 0x0,      // least recently used
    LIRS = 0x1,     // low inter-reference recency set
    ClockPro = 0x2, // CLOCK-Pro
    Arc = 0x3,      // adaptive replacement cache
};

// Configuration for a database handle.  Default-constructed Options are
// usable as-is: every interface and enum has a working default.
//
// Several knobs come in pairs: an enum selects an implementation
// (`active_compression_type`, `active_filter_type`, `active_map`,
// `table_cache_type`) and a matching interface member holds the resolved
// implementation (`compressor`, `filter`).  Use new_compressor()/new_filter()
// to materialize an implementation from an enum value.
struct ANCHORDB_EXPORT Options {
    Options() = default;

    // Environment used for all I/O.  Defaults to the platform Env; the database
    // does not take ownership.
    Env *env = Env::default_env();
    // Optional diagnostics logger.  Not owned by the database.
    Logger *info_log = nullptr;

    // Key ordering.  Defaults to byte-wise comparison, and must match the
    // comparator the database was created with.
    Comparator comparator = new_byte_wise_comparator();

    // Block compression.  `active_compression_type` selects the codec and
    // `compressor` holds the resolved implementation (a null compressor stores
    // blocks verbatim); `compression_level` is passed through to the codec.
    CompressionType active_compression_type = CompressionType::None;
    Compressor compressor = Compressor{};
    int compression_level = 1;

    // Per-table filter.  `active_filter_type` selects the algorithm and
    // `filter` holds the resolved implementation (a null filter disables
    // filtering).  Defaults to filtering disabled; assign
    // new_filter(active_filter_type) to enable it.
    FilterType active_filter_type = FilterType::Bloom;
    Filter filter = Filter{};

    // Hash-map backend used by the cache implementations.
    MapType active_map = MapType::Swiss;

    // Open-table cache: capacity in bytes and the replacement policy used to
    // implement it (see TableCache).
    size_t table_cache_size = 8 * 1024 * 1024;
    CacheType table_cache_type = CacheType::LRU;
    // Optional shared block cache.  Not owned by the database.
    Cache *block_cache = nullptr;

    // Size at which the active memtable is flushed to an on-disk table.
    size_t write_buffer_size = 4 * 1024 * 1024;
    // Logical size of an on-disk data block.
    size_t block_size = 4 * 1024;
    // Approximate maximum size of a table file before the table is split.
    size_t max_file_size = 2 * 1024 * 1024;
    // Upper bound on concurrently open file descriptors.
    int max_open_files = 1000;
};

// Per-read tuning options.
struct ANCHORDB_EXPORT ReadOptions {
    // Verify block checksums on read.  Stronger, but slower.
    bool verify_checksums = false;
    // Whether blocks read are inserted into the block cache.
    bool fill_cache = true;
};

// Per-write tuning options.
struct ANCHORDB_EXPORT WriteOptions {
    WriteOptions() = default;

    // If true, fsync the write-ahead log before the write is acknowledged as
    // durable.
    bool sync = false;
};

} // namespace ANCHORDB_NAMESPACE
