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
#include "src/memtable/arena.h"
#include "src/memtable/key.h"
#include "src/util/coding.h"
#include "table.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

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

void MemTable::add(SeqNum s, ValueType type, const Slice &key,
                   const Slice &value) {
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
    return true; // not implemented yet
}

} // namespace anchordb
