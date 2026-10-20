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

#include "anchordb/anchordb_namespace.h"
#include "anchordb/status.h"
#include "db/db_format.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ANCHORDB_NAMESPACE {

// Self-describing record format shared by data.bin and the WiscKey value log
// (vLog), so recovery, integrity checks and vLog GC share one codec.
//
// clang-format off
/*
    +-------------------+-------------+----------------+-------------------+-----------------+---------------------+-----------+-----------+-----------------------+
    | magic_number (4B) | crc32 (4B)  | timestamp (8B) | key_size (Varint) |    key_bytes    | value_size (Varint) | type (1B) | flag (1B) |      value_bytes      |
    +-------------------+-------------+----------------+-------------------+-----------------+---------------------+-----------+-----------+-----------------------+
                                                                             [Raw User Key]     [type | flag | ]                             [Raw String Bytes OR]
                                                                                                  [value_bytes]                                 [WiscKey Handle]
*/
// clang-format on
//
// Field details:
//   - magic_number: kDataRecordMagic, stored little-endian.
//   - crc32:        masked CRC32 (see util/crc32.h) computed over every byte
//                   that follows the crc32 field (timestamp .. value_bytes).
//   - timestamp:    uint64_t, little-endian.
//   - type:         ValueType byte (value / deletion).
//   - flag:         ValueFlag byte (inline_ / blob).  When flag == blob the
//                   value_bytes hold an encoded BlobHandle (see
//                   encode_blob_handle) instead of the raw value.
inline constexpr uint32_t kDataRecordMagic = 0x42444E41u; // "ANDB"

inline constexpr size_t kDataRecordMagicSize = sizeof(uint32_t);     // 4
inline constexpr size_t kDataRecordCrcSize = sizeof(uint32_t);       // 4
inline constexpr size_t kDataRecordTimestampSize = sizeof(uint64_t); // 8

// Fixed prefix before key_size: magic + crc + timestamp.
inline constexpr size_t kDataRecordHeaderSize =
    kDataRecordMagicSize + kDataRecordCrcSize + kDataRecordTimestampSize; // 16

// type + flag.
inline constexpr size_t kDataRecordValueMetaSize =
    sizeof(uint8_t) + sizeof(uint8_t); // 2

// A fully decoded record.  `key` and `value` are zero-copy views into the
// buffer that was decoded and must not outlive it.
struct ParsedDataRecord {
    std::string_view key;
    std::string_view value;
    ValueType type = ValueType::value;
    ValueFlag flag = ValueFlag::inline_;
    uint64_t timestamp = 0;

    // True when value holds an encoded BlobHandle (a WiscKey pointer) rather
    // than the raw value bytes.
    bool is_blob() const { return flag == ValueFlag::blob; }
    // True when this record marks a deleted key.
    bool is_deletion() const { return type == ValueType::deletion; }
};

// Returns the exact number of bytes encode_data_record() will append for a
// record with the given key and value sizes.  Useful for pre-reserving output.
size_t data_record_encoded_len(size_t key_size, size_t value_size);

// Appends one record to *dst and returns the number of bytes appended.  The
// CRC is computed and patched in after the payload is written.
//
// `value_bytes` is the raw value when flag == inline_, or an already-encoded
// BlobHandle when flag == blob.
size_t encode_data_record(std::string *dst, uint64_t timestamp,
                          std::string_view key, std::string_view value_bytes,
                          ValueType type, ValueFlag flag);

// Convenience overload that encodes `handle` as the value and sets
// flag == ValueFlag::blob.
size_t encode_data_record(std::string *dst, uint64_t timestamp,
                          std::string_view key, const BlobHandle &handle,
                          ValueType type = ValueType::value);

// Decodes one record from the front of *input and advances *input past it.  On
// success fills *result; on failure *input is left unchanged.  Magic, all
// lengths and the CRC are validated before *result is populated.
Status decode_data_record(std::string_view *input, ParsedDataRecord *result);

// Convenience overload that decodes from a fixed view without a cursor.
Status decode_data_record(std::string_view input, ParsedDataRecord *result);

// Computes the total encoded size of the record at the front of `input` without
// requiring the key/value payload to be present.  Sets *complete = false (with
// *total_size as a minimum retry length) while too few bytes are available.
Status data_record_size(std::string_view input, size_t *total_size,
                        bool *complete);

} // namespace ANCHORDB_NAMESPACE