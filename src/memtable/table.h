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
#include "anchordb/slice.h"
#include "anchordb/status.h"
#include "src/memtable/arena.h"
#include "src/memtable/key.h"
#include "src/memtable/skiplist.h"
#include "src/util/coding.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>

namespace anchordb {

class MemTableIterator;
class InternalKeyComparator;

namespace {
Slice get_len_prefixed_slice(const char *data) {
    uint32_t len;
    const char *p = data;
    p = get_varint_32_ptr(p, p + 5, &len);
    return Slice(p, len);
}

char *encode_key(std::string *scratch, const Slice &target) {
    scratch->clear();
    put_varint_32(scratch, target.size());
    scratch->append(target.data(), target.size());
    return scratch->data();
}
} // namespace

class MemTable {
  public:
    explicit MemTable(const InternalKeyComparator &comparator,
                      const uint32_t arena_block_size);

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

    size_t approximate_memory_usage() const;
    Iterator *new_iterator();

    void add(SeqNum seq, ValueType type, const Slice &key, const Slice &value);
    bool get(const LookUpKey &key, std::string *value, Status *s);

  private:
    friend class MemTableIterator;

    struct KeyComparator {
        const InternalKeyComparator comparator;
        explicit KeyComparator(const InternalKeyComparator &c)
            : comparator(c) {}
        int operator()(const char *a, const char *b) const;
    };

    using Table = Skiplist<const char *, KeyComparator>;

    ~MemTable();

    KeyComparator comparator_;
    int refs_;
    Arena arena_;
    Table table_;
};

} // namespace anchordb
