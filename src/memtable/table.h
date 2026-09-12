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

#include "anchordb/slice.h"
#include "anchordb/status.h"
#include "arena.h"
#include "db/db_format.h"
#include "skiplist.h"
#include "util/coding.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>

namespace anchordb {

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

template <typename Comparator> class MemTable {
  private:
    struct KeyComparator {
        const Comparator comparator;
        explicit KeyComparator(const Comparator &c) : comparator(c) {}
        int operator()(const char *aptr, const char *bptr) const {
            Slice a = get_len_prefixed_slice(aptr);
            Slice b = get_len_prefixed_slice(bptr);
            return comparator.compare(a, b);
        }
    };

  public:
    using Table = Skiplist<const char *, KeyComparator>;

    explicit MemTable(const Comparator &comparator,
                      const uint32_t arena_block_size)
        : comparator_(comparator), refs_(0), arena_(arena_block_size),
          table_(KeyComparator(comparator_), &arena_) {}

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

    class Iterator {
      public:
        explicit Iterator(typename MemTable::Table *table) : iter_(table) {}

        Iterator(const Iterator &) = delete;
        Iterator &operator=(const Iterator &) = delete;

        ~Iterator() = default;

        bool valid() const { return iter_.valid(); }
        void next() { iter_.next(); }
        void prev() { iter_.prev(); }
        void seek(const Slice &key) { iter_.seek(encode_key(&tmp_, key)); }
        void seek_to_first() { iter_.seek_to_first(); }
        void seek_to_last() { iter_.seek_to_last(); }
        Slice key() const { return get_len_prefixed_slice(iter_.key()); }
        Slice value() const {
            Slice key_slice = get_len_prefixed_slice(iter_.key());
            return get_len_prefixed_slice(key_slice.data() + key_slice.size());
        }
        Status status() const { return Status::ok(); }

      private:
        typename Table::Iterator iter_;
        std::string tmp_;
    };

    size_t approximate_memory_usage() const { return arena_.memory_usage(); }

    void add(SeqNum seq, ValueType type, const Slice &key, const Slice &value) {
        // Format of an entry is concatenation of:
        //  key_size     : varint32 of internal_key.size()
        //  key bytes    : char[internal_key.size()]
        //  tag          : uint64((sequence << 8) | type)
        //  value_size   : varint32 of value.size()
        //  value bytes  : flag + char[value.size() - 1]
        size_t key_size = key.size();
        size_t value_size = value.size();
        size_t internal_key_size = key_size + 8;
        const size_t encoded_len = varint_len(key_size) + internal_key_size +
                                   varint_len(value_size) + value_size;
        char *buf = arena_.allocate(encoded_len);
        char *p = encode_varint_32(buf, internal_key_size);
        std::memcpy(p, key.data(), key.size());
        p += key_size;
        encode_fixed_64(p, (seq << 8) | type);
        p += 8;
        p = encode_varint_32(p, value_size);
        std::memcpy(p, value.data(), value_size);
        assert(p + value_size == buf + encoded_len);
        table_.insert(buf);
    }

    bool get(const LookUpKey &key, std::string *value, Status *s) {
        Slice mem_key = key.memtable_key();
        typename Table::Iterator iter(&table_);
        iter.seek(mem_key.data());
        if (iter.valid()) {
            // entry format is:
            //    klength  : varint32
            //    userkey  : char[klength]
            //    tag      : uint64
            //    vlength  : varint32
            //    value    : flag + char[vlength - 1]
            const char *entry = iter.key();
            uint32_t key_len;
            const char *key_ptr = get_varint_32_ptr(entry, entry + 5, &key_len);
            if (comparator_.comparator.user_comparator()->compare(
                    Slice(key_ptr, key_len - 8), key.user_key()) == 0) {
                const uint64_t tag = decode_fixed_64(key_ptr + key_len - 8);
                switch (static_cast<ValueType>(tag & 0xff)) {
                case TypeValue: {
                    Slice v = get_len_prefixed_slice(key_ptr + key_len);
                    value->assign(v.data(), v.size());
                    return true;
                }
                case TypeDeletion: {
                    *s = Status::not_found(Slice());
                    return false;
                }
                }
            }
        }
        return false;
    }

  private:
    ~MemTable() { assert(refs_ == 0); }

    Comparator comparator_;
    int refs_;
    Arena arena_;
    Table table_;
};

} // namespace anchordb
