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

#include "anchordb/slice.h"
#include "anchordb/status.h"
#include "src/memtable/arena.h"
#include "src/memtable/key.h"
#include "src/util/coding.h"
#include "table.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sys/types.h>

namespace anchordb {

MemTable::MemTable(const InternalKeyComparator &comparator,
                   const uint32_t arena_block_size)
    : comparator_(comparator), refs_(0), arena_(arena_block_size),
      table_(comparator_, &arena_) {}
MemTable::~MemTable() { assert(refs_ == 0); }

size_t MemTable::approximate_memory_usage() const {
    return arena_.memory_usage();
}

int MemTable::KeyComparator::operator()(const char *aptr,
                                        const char *bptr) const {
    Slice a = get_len_prefixed_slice(aptr);
    Slice b = get_len_prefixed_slice(bptr);
    return comparator.compare(a, b);
}

class MemTableIterator : public Iterator {
  public:
    explicit MemTableIterator(MemTable::Table *table) : iter_(table) {}

    MemTableIterator(const MemTableIterator &) = delete;
    MemTableIterator &operator=(const MemTableIterator &) = delete;

    ~MemTableIterator() override = default;

    bool valid() const override { return iter_.valid(); }
    void next() override { iter_.next(); }
    void prev() override { iter_.prev(); }
    void seek(const Slice &key) override { iter_.seek(encode_key(&tmp_, key)); }
    void seek_to_first() override { iter_.seek_to_first(); }
    void seek_to_last() override { iter_.seek_to_last(); }
    Slice key() const override { return get_len_prefixed_slice(iter_.key()); }
    Slice value() const override {
        Slice key_slice = get_len_prefixed_slice(iter_.key());
        return get_len_prefixed_slice(key_slice.data() + key_slice.size());
    }
    Status status() const override { return Status::ok(); }

  private:
    MemTable::Table::Iterator iter_;
    std::string tmp_;
};

Iterator *MemTable::new_iterator() { return new MemTableIterator(&table_); }

void MemTable::add(SeqNum s, ValueType type, const Slice &key,
                   const Slice &value) {
    // Format of an entry is concatenation of:
    //  key_size     : varint32 of internal_key.size()
    //  key bytes    : char[internal_key.size()]
    //  tag          : uint64((sequence << 8) | type)
    //  value_size   : varint32 of value.size()
    //  value bytes  : char[value.size()]
    size_t key_size = key.size();
    size_t value_size = value.size();
    size_t internal_key_size = key_size + 8;
    const size_t encoded_len = varint_len(key_size) + internal_key_size +
                               varint_len(value_size) + value_size;
    char *buf = arena_.allocate(encoded_len);
    char *p = encode_varint_32(buf, internal_key_size);
    std::memcpy(p, key.data(), key.size());
    p += key_size;
    encode_fixed_64(p, (s << 8) | type);
    p += 8;
    std::memcpy(p, value.data(), value.size());
    assert(p + value_size == buf + encoded_len);
    table_.insert(buf);
}

bool MemTable::get(const LookUpKey &key, std::string *value, Status *s) {
    Slice mem_key = key.memtable_key();
    Table::Iterator iter(&table_);
    iter.seek(mem_key.data());
    if (iter.valid()) {
        // entry format is:
        //    klength  varint32
        //    userkey  char[klength]
        //    tag      uint64
        //    vlength  varint32
        //    value    char[vlength]
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

} // namespace anchordb
