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

#include "memtable/table.h"

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

void MemTable::add(seq_num seq, ValueType type, std::string_view key,
                   std::string_view value) {
    // Entry layout:
    //  internal_key_size : varint32
    //  user_key          : char[key_size]
    //  tag               : uint64((sequence << 8) | type)
    //  value_size        : varint32
    //  value             : char[value_size]
    size_t key_size = key.size();
    size_t value_size = value.size();
    size_t internal_key_size = key_size + 8;
    const size_t encoded_len = varint_len(key_size) + internal_key_size +
                               varint_len(value_size) + value_size;
    char *buf = arena_.allocate(encoded_len);
    char *p = encode_varint_32(buf, internal_key_size);
    std::memcpy(p, key.data(), key.size());
    p += key_size;
    encode_fixed_64(p, (seq << 8) | static_cast<uint64_t>(type));
    p += 8;
    p = encode_varint_32(p, value_size);
    std::memcpy(p, value.data(), value_size);
    assert(p + value_size == buf + encoded_len);
    table_.insert(buf);
}

bool MemTable::get(const LookUpKey &key, std::string *value, Status *s) {
    std::string_view mem_key = key.memtable_key();
    typename Table::Iterator iter(&table_);
    iter.seek(mem_key.data());
    if (iter.valid()) {
        // Entry layout:
        //    internal_key_size : varint32
        //    user_key          : char[internal_key_size - 8]
        //    tag               : uint64
        //    value_size        : varint32
        //    value             : char[value_size]
        const char *entry = iter.key();
        uint32_t key_len;
        const char *key_ptr = get_varint_32_ptr(entry, entry + 5, &key_len);
        if (comparator_.compare(std::string_view(key_ptr, key_len - 8),
                                key.user_key()) == 0) {
            const uint64_t tag = decode_fixed_64(key_ptr + key_len - 8);
            switch (static_cast<ValueType>(tag & 0xff)) {
            case ValueType::value: {
                std::string_view v = get_len_prefixed_slice(key_ptr + key_len);
                value->assign(v.data(), v.size());
                return true;
            }
            case ValueType::deletion: {
                *s = Status::not_found(std::string_view());
                return false;
            }
            }
        }
    }
    return false;
}

} // namespace ANCHORDB_NAMESPACE
