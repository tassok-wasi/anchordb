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

#include "anchordb/iterator.h"
#include "anchordb/status.h"
#include "arena.h"
#include "db/db_format.h"
#include "memtable/skiplist.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

// Decodes a length-prefixed slice: varint32 length followed by the payload.
inline std::string_view get_len_prefixed_slice(const char *data) {
    uint32_t len;
    const char *p = data;
    p = get_varint_32_ptr(p, p + 5, &len);
    return std::string_view(p, len);
}

// Encodes `target` into `scratch` as a length-prefixed slice.
inline char *encode_key(std::string *scratch, std::string_view target) {
    scratch->clear();
    put_varint_32(scratch, target.size());
    scratch->append(target.data(), target.size());
    return scratch->data();
}

// Compares two length-prefixed keys using the underlying user comparator.
struct KeyComparator {
    const Comparator comparator;
    explicit KeyComparator(const Comparator &c) : comparator(c) {}
    int operator()(const char *aptr, const char *bptr) const {
        std::string_view a = get_len_prefixed_slice(aptr);
        std::string_view b = get_len_prefixed_slice(bptr);
        return comparator.compare(a, b);
    }
};

using Table = Skiplist<const char *, KeyComparator>;

// Iterator over a memtable's skiplist, decoding length-prefixed keys/values.
class MemtableIterator : public Iterator {
  public:
    explicit MemtableIterator(Table *table) : iter_(table) {}

    MemtableIterator(MemtableIterator &&other) noexcept
        : iter_(std::move(other.iter_)), tmp_(std::move(other.tmp_)) {}

    MemtableIterator &operator=(MemtableIterator &&other) {
        if (this != &other) {
            this->iter_ = std::move(other.iter_);
            this->tmp_ = std::move(other.tmp_);
        }
        return *this;
    }

    ~MemtableIterator() = default;

    bool valid() const override { return iter_.valid(); }
    void next() override { iter_.next(); }
    void prev() override { iter_.prev(); }
    void seek(std::string_view key) override {
        iter_.seek(encode_key(&tmp_, key));
    }
    void seek_to_first() override { iter_.seek_to_first(); }
    void seek_to_last() override { iter_.seek_to_last(); }
    std::string_view key() const override {
        return get_len_prefixed_slice(iter_.key());
    }
    std::string_view value() const override {
        // Value follows the key in the same length-prefixed entry.
        std::string_view key_slice = get_len_prefixed_slice(iter_.key());
        return get_len_prefixed_slice(key_slice.data() + key_slice.size());
    }
    Status status() const override { return Status::ok(); }

  private:
    typename Table::Iterator iter_;
    std::string tmp_; // scratch buffer for seek key encoding
};

// In-memory table backed by a skiplist and arena allocator.
class MemTable {
  public:
    explicit MemTable(const Comparator comparator,
                      const uint32_t arena_block_size)
        : comparator_(std::move(comparator)), refs_(0),
          arena_(arena_block_size), table_(KeyComparator(comparator_), &arena_),
          iter_(std::move(&table_)) {}

    MemTable(const MemTable &) = delete;
    MemTable &operator=(const MemTable &) = delete;

    // Reference counting for shared ownership.
    void ref() { ++refs_; }
    void unref() {
        --refs_;
        assert(refs_ >= 0);
        if (refs_ <= 0) { delete this; }
    }

    size_t approximate_memory_usage() const { return arena_.memory_usage(); }

    Iterator *new_iterator() { return new MemtableIterator(&table_); }

    void add(seq_num seq, ValueType type, std::string_view key,
             std::string_view value);
    bool get(const LookUpKey &key, std::string *value, Status *s);

  private:
    ~MemTable() { assert(refs_ == 0); }

    Comparator comparator_;
    Arena arena_;
    int refs_;
    Table table_;
    MemtableIterator iter_;
};

} // namespace ANCHORDB_NAMESPACE
