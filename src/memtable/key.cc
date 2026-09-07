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
#include "key.h"
#include "util/coding.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

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

// Appends the internal key encoding to *result: user_key bytes followed by
// the 8-byte packed sequence+type trailer.
void append_internal_key(std::string *result, const ParsedInternalKey &key) {
    result->append(key.user_key.data(), key.user_key.size());
    put_fixed_64(result, pack_seq_and_type(key.sequence, key.type));
}

namespace {
void put_fixed_32(char *dst, uint32_t value) {
    char buf[4];
    encode_fixed_32(buf, value);
    memcpy(dst, buf, 4);
}

void put_fixed_64(char *dst, uint64_t value) {
    char buf[8];
    encode_fixed_64(buf, value);
    memcpy(dst, buf, 8);
}
} // namespace

void append_blob_address(char *result, const ParsedBlobAddress &addr) {
    put_fixed_64(result, addr.offset);
    put_fixed_32(result + 8, addr.file_id);
    put_fixed_32(result + 12, addr.size);
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
