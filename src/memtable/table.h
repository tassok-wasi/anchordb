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

#include "anchordb/status.h"
#include "arena.h"
#include "db/db_format.h"
#include "table_iter.h"
#include "table_util.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace anchordb {

class MemTable {
  public:
    explicit MemTable(const Comparator comparator,
                      const uint32_t arena_block_size)
        : comparator_(std::move(comparator)), refs_(0),
          arena_(arena_block_size), table_(KeyComparator(comparator_), &arena_),
          iter_(std::move(&table_)) {}

    MemTable(const MemTable &) = delete;
    MemTable &operator=(const MemTable &) = delete;

    void ref() { ++refs_; }
    void unref() {
        --refs_;
        assert(refs_ >= 0);
        if (refs_ <= 0) {
            delete this;
        }
    }

    size_t approximate_memory_usage() const { return arena_.memory_usage(); }

    Iterator new_iterator() { return new_memtable_iterator(&iter_); }

    void add(seq_num seq, ValueType type, std::string_view key,
             std::string_view value);
    bool get(const LookUpKey &key, std::string *value, Status *s);

  private:
    ~MemTable() { assert(refs_ == 0); }

    Comparator comparator_;
    MemtableIterator iter_;
    int refs_;
    Arena arena_;
    Table table_;
};

} // namespace anchordb
