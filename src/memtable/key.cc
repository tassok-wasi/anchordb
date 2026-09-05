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

#include "key.h"
#include "util/coding.h"
#include <anchordb/slice.h>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace anchordb {

// Returns the on-disk size: user_key bytes + 8-byte (seq+type) trailer.
size_t internal_key_encoding_len(const ParsedInternalKey &key) {
    return key.user_key.size() + 8;
}

// Decodes the trailing 8 bytes of an internal key into sequence number and
// value type. Layout: | user_key (n-8 bytes) | sequence (7 bytes, big-endian) |
// type (1 byte) |
bool parse_internal_key(const Slice &internal_key, ParsedInternalKey *result) {
    const size_t n = internal_key.size();
    if (n < 8)
        return false;

    uint64_t num = decode_fixed_64(internal_key.data());
    uint8_t c = num & 0xff;
    result->sequence = num >> 8;
    result->type = static_cast<ValueType>(c);
    result->user_key = Slice(internal_key.data(), n - 8);
    return (c <= static_cast<uint8_t>(ValueType::TypeValue));
}

// Strips the trailing 8-byte trailer and returns only the user_key portion.
Slice extract_user_key(const Slice &internal_key) {
    assert(internal_key.size() >= 8);
    return Slice(internal_key.data(), internal_key.size() - 8);
}

namespace {
// Packs a 56-bit sequence number and a 1-byte ValueType into a single uint64_t.
// Layout: | sequence (56 bits) | type (8 bits) |
uint64_t pack_seq_and_type(uint64_t seq, ValueType t) {
    assert(seq <= max_seq_num);
    assert(t <= ValueType::TypeValue);
    return (seq << 8) | static_cast<uint64_t>(t);
}
} // namespace

// Appends the internal key encoding to *result: user_key bytes followed by
// the 8-byte packed sequence+type trailer.
void append_internal_key(std::string *result, const ParsedInternalKey &key) {
    result->append(key.user_key.data(), key.user_key.size());
    put_fixed_64(result, pack_seq_and_type(key.sequence, key.type));
}

const char *InternalKeyComparator::name() const {
    return "anchodb.InternalKeyComparator";
}

// Orders by user_key ascending, then by sequence number descending.
// When two internal keys share the same user_key, the one with the larger
// sequence number (newer write) sorts first.
int InternalKeyComparator::compare(const Slice &a, const Slice &b) const {
    int r = user_comparator_->compare(extract_user_key(a), extract_user_key(b));
    if (r == 0) {
        const uint64_t anum = decode_fixed_64(a.data() + a.size() - 8);
        const uint64_t bnum = decode_fixed_64(b.data() + b.size() - 8);
        if (anum > bnum) {
            return -1;
        } else if (anum < bnum) {
            return 1;
        }
    }
    return r;
}

// Constructs a memtable lookup key.  The wire format is:
//   | varint32(internal_key_size) | user_key | packed(seq, TypeValue) |
// The varint32 length prefix is required by the memtable's internal encoding.
LookUpKey::LookUpKey(const Slice &user_key, SeqNum seq) {
    size_t usize = user_key.size();
    size_t needed = usize + 13;
    char *dst;
    if (needed <= sizeof(space_))
        dst = space_;
    else
        dst = new char[needed];
    start_ = dst;
    dst = encode_varint_32(dst, usize + 8);
    kstart_ = dst;
    std::memcpy(dst, user_key.data(), usize);
    dst += usize;
    encode_fixed_64(dst, pack_seq_and_type(seq, ValueType::TypeValue));
    dst += 8;
    end_ = dst;
}

} // namespace anchordb
