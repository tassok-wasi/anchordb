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

// Unified self-describing record format shared by data.bin and the WiscKey
// value log (vLog).  Both files use the exact same layout so that crash
// recovery, integrity verification and vLog garbage collection can share one
// encoder/decoder.
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

// Appends one record to *dst and returns the number of bytes appended.
//
// `value` is opaque payload bytes: the raw value when flag == inline_, or an
// already-encoded BlobHandle when flag == blob (the convenience overload below
// handles that case).  The CRC is computed and patched in after the payload is
// written, so callers never have to compute it themselves.
size_t encode_data_record(std::string *dst, uint64_t timestamp,
                          std::string_view key, std::string_view value_bytes,
                          ValueType type, ValueFlag flag);

// Convenience overload that encodes `handle` as the value and sets
// flag == ValueFlag::blob.
size_t encode_data_record(std::string *dst, uint64_t timestamp,
                          std::string_view key, const BlobHandle &handle,
                          ValueType type = ValueType::value);

// Decodes one record from the front of *input and advances *input past it.
//
// On success fills *result and returns Status::ok(); on failure *input is left
// unchanged and a corruption Status is returned (invalid_argument if a pointer
// argument is null).  All lengths and the trailing metadata are validated
// against the remaining bytes, the magic number is checked, and the masked
// CRC32 is verified before *result is populated.
Status decode_data_record(std::string_view *input, ParsedDataRecord *result);

// Convenience overload that decodes from a fixed view without a cursor.
Status decode_data_record(std::string_view input, ParsedDataRecord *result);

// Determines the total encoded size (in bytes) of the record at the front of
// `input`, so a caller that does not yet hold the whole record can size a
// read.  Only the fixed header and the two length varints must be reachable;
// the key/value payload bytes need not be present.
//
// On success sets *total_size and *complete = true.  When `input` does not yet
// contain enough bytes to reach the value_size varint, sets *complete = false
// and *total_size to the minimum number of bytes (from the record's first byte)
// the caller should supply before retrying; returns Status::ok().  Returns
// Status::corruption if the fixed header is present but malformed, or if a
// length field is implausibly large.  Returns Status::invalid_argument if an
// output pointer is null.
Status data_record_size(std::string_view input, size_t *total_size,
                        bool *complete);

} // namespace ANCHORDB_NAMESPACE