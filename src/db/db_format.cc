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

#include "db_format.h"
#include "util/coding.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

// Decodes the trailing 8 bytes of an internal key into sequence number and
// value type. Layout: | user_key (n-8 bytes) | sequence (7 bytes,
// little-endian) | type (1 byte) |
bool parse_internal_key(std::string_view internal_key,
                        ParsedInternalKey *result) {
    const size_t n = internal_key.size();
    if (n < 8) return false;

    uint64_t num = decode_fixed_64(internal_key.data());
    uint8_t c = num & 0xff;
    result->sequence = num >> 8;
    result->type = static_cast<ValueType>(c);
    result->user_key = std::string_view(internal_key.data(), n - 8);
    return (c <= static_cast<uint8_t>(ValueType::value));
}

// Constructs a memtable lookup key.  The wire format is:
//   | varint32(internal_key_size) | user_key | packed(seq, TypeValue) |
// The varint32 length prefix is required by the memtable's internal encoding.
LookUpKey::LookUpKey(std::string_view user_key, seq_num seq) {
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
    encode_fixed_64(dst, pack_seq_and_type(seq, ValueType::value));
    dst += 8;
    end_ = dst;
}

} // namespace ANCHORDB_NAMESPACE
