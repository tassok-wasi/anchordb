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

#include "anchordb/cache.h"
#include "anchordb/status.h"
#include "sstable/table.h"
#include "sstable/table_cache.h"
#include "util/coding.h"
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

namespace {

// Builds the concrete Cache implementation selected by the options.
std::unique_ptr<Cache> make_cache(const Options &options) {
    switch (options.table_cache_type) {
    case CacheType::LIRS:
        return std::unique_ptr<Cache>(
            new_lirs_cache(options.table_cache_size, options.active_map));
    case CacheType::ClockPro:
        return std::unique_ptr<Cache>(
            new_clock_pro_cache(options.table_cache_size, options.active_map));
    case CacheType::LRU:
    default:
        return std::unique_ptr<Cache>(
            new_lru_cache(options.table_cache_size, options.active_map));
    }
}

} // namespace

TableCache::TableCache(const Options &options, std::string db_dir)
    : options_(options), db_dir_(std::move(db_dir)),
      cache_(make_cache(options_)), namespace_id_(cache_.new_id()) {}

TableCache::~TableCache() = default;

std::string TableCache::cache_key(uint64_t table_number) const {
    std::string key;
    key.reserve(2 * sizeof(uint64_t));
    char buf[sizeof(uint64_t)];
    encode_fixed_64(buf, namespace_id_);
    key.append(buf, sizeof(buf));
    encode_fixed_64(buf, table_number);
    key.append(buf, sizeof(buf));
    return key;
}

void TableCache::delete_entry(std::string_view, void *value) {
    delete static_cast<std::shared_ptr<const Table> *>(value);
}

Status TableCache::find_table(uint64_t table_number,
                              std::shared_ptr<const Table> *out) {
    if (out == nullptr) {
        return Status::invalid_argument("TableCache::find_table: null output");
    }

    const std::string key = cache_key(table_number);
    Cache::Handle *handle = cache_.lookup(key);
    if (handle != nullptr) {
        auto *holder =
            static_cast<std::shared_ptr<const Table> *>(cache_.value(handle));
        *out = *holder;
        cache_.release(handle);
        return Status::ok();
    }

    const TableFileSet files = make_table_file_set(db_dir_, table_number);
    std::shared_ptr<Table> table;
    Status s = Table::open(options_, files, &table);
    if (!s.is_ok()) { return s; }

    auto shared = std::shared_ptr<const Table>(table);
    auto *holder = new std::shared_ptr<const Table>(shared);
    Cache::Handle *inserted =
        cache_.insert(key, holder, shared->cache_charge(), &delete_entry);
    if (inserted != nullptr) {
        auto *stored =
            static_cast<std::shared_ptr<const Table> *>(cache_.value(inserted));
        *out = *stored;
        cache_.release(inserted);
    } else {
        // Cache is disabled (zero capacity): hand back the table directly.
        *out = std::move(shared);
    }
    return Status::ok();
}

Status TableCache::get(const ReadOptions &options, uint64_t table_number,
                       std::string_view key, std::string *value,
                       std::string *scratch) {
    std::shared_ptr<const Table> table;
    Status s = find_table(table_number, &table);
    if (!s.is_ok()) { return s; }
    return table->get(options, key, value, scratch);
}

Status TableCache::new_iterator(const ReadOptions &options,
                                uint64_t table_number, Iterator **iter) {
    if (iter == nullptr) {
        return Status::invalid_argument(
            "TableCache::new_iterator: null iterator output");
    }
    std::shared_ptr<const Table> table;
    Status s = find_table(table_number, &table);
    if (!s.is_ok()) { return s; }
    *iter = table->new_iterator(options);
    return Status::ok();
}

Status TableCache::erase(uint64_t table_number) {
    cache_.erase(cache_key(table_number));
    return Status::ok();
}

} // namespace ANCHORDB_NAMESPACE
