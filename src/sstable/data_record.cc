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

#include "sstable/data_record.h"
#include "util/coding.h"
#include "util/crc32.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

namespace {

// Byte offset of the CRC field within a record (immediately after the magic).
constexpr size_t kCrcFieldOffset = kDataRecordMagicSize;

// Byte offset at which the CRC-covered region begins (immediately after CRC).
constexpr size_t kCrcCoveredOffset = kDataRecordMagicSize + kDataRecordCrcSize;

} // namespace

size_t data_record_encoded_len(size_t key_size, size_t value_size) {
    return kDataRecordHeaderSize + static_cast<size_t>(varint_len(key_size)) +
           key_size + static_cast<size_t>(varint_len(value_size)) +
           kDataRecordValueMetaSize + value_size;
}

size_t encode_data_record(std::string *dst, uint64_t timestamp,
                          std::string_view key, std::string_view value_bytes,
                          ValueType type, ValueFlag flag) {
    assert(dst != nullptr);

    const size_t start = dst->size();

    // Fixed header: magic, CRC placeholder, timestamp.
    put_fixed_32(dst, kDataRecordMagic);
    put_fixed_32(dst, 0); // CRC patched below once the payload exists.
    put_fixed_64(dst, timestamp);

    // Key.
    put_varint_64(dst, key.size());
    dst->append(key.data(), key.size());

    // Value metadata and payload.
    put_varint_64(dst, value_bytes.size());
    dst->push_back(static_cast<char>(static_cast<uint8_t>(type)));
    dst->push_back(static_cast<char>(static_cast<uint8_t>(flag)));
    dst->append(value_bytes.data(), value_bytes.size());

    // The CRC covers every byte after the CRC field, i.e. timestamp through
    // value_bytes.
    const char *payload = dst->data() + start + kCrcCoveredOffset;
    const size_t payload_size = dst->size() - (start + kCrcCoveredOffset);
    const uint32_t crc = mask(value(payload, payload_size));
    encode_fixed_32(&(*dst)[start + kCrcFieldOffset], crc);

    return dst->size() - start;
}

size_t encode_data_record(std::string *dst, uint64_t timestamp,
                          std::string_view key, const BlobHandle &handle,
                          ValueType type) {
    std::string encoded_handle;
    BlobHandle copy = handle;
    encode_blob_handle(&copy, &encoded_handle);
    return encode_data_record(dst, timestamp, key, encoded_handle, type,
                              ValueFlag::blob);
}

Status decode_data_record(std::string_view *input, ParsedDataRecord *result) {
    if (input == nullptr) {
        return Status::invalid_argument("decode_data_record: null input");
    }
    if (result == nullptr) {
        return Status::invalid_argument("decode_data_record: null result");
    }

    const std::string_view record = *input;

    // A record must at least carry the fixed header.
    if (record.size() < kDataRecordHeaderSize) {
        return Status::corruption("data record truncated: shorter than header",
                                  std::to_string(record.size()));
    }

    // Magic number.
    if (decode_fixed_32(record.data()) != kDataRecordMagic) {
        return Status::corruption("data record bad magic number");
    }

    const uint32_t stored_crc =
        decode_fixed_32(record.data() + kCrcFieldOffset);

    // Walk the payload.  The record length is not stored explicitly, so it is
    // derived by parsing; every bound below is checked against the remaining
    // bytes so a truncated or malicious record can never cause an over-read.
    std::string_view cursor = record.substr(kCrcCoveredOffset);

    const uint64_t timestamp = decode_fixed_64(cursor.data());
    cursor.remove_prefix(kDataRecordTimestampSize);

    uint64_t key_size = 0;
    if (!get_varint_64(&cursor, &key_size)) {
        return Status::corruption("data record truncated key_size");
    }
    if (key_size > cursor.size()) {
        return Status::corruption(
            "data record key_size exceeds remaining bytes");
    }
    const std::string_view key =
        cursor.substr(0, static_cast<size_t>(key_size));
    cursor.remove_prefix(static_cast<size_t>(key_size));

    uint64_t value_size = 0;
    if (!get_varint_64(&cursor, &value_size)) {
        return Status::corruption("data record truncated value_size");
    }

    if (cursor.size() < kDataRecordValueMetaSize) {
        return Status::corruption("data record missing type/flag");
    }
    const uint8_t type_byte = static_cast<uint8_t>(cursor[0]);
    const uint8_t flag_byte = static_cast<uint8_t>(cursor[1]);
    cursor.remove_prefix(kDataRecordValueMetaSize);

    if (type_byte > static_cast<uint8_t>(ValueType::value)) {
        return Status::corruption("data record invalid value type");
    }
    if (flag_byte > static_cast<uint8_t>(ValueFlag::inline_)) {
        return Status::corruption("data record invalid value flag");
    }

    if (value_size > cursor.size()) {
        return Status::corruption(
            "data record value_size exceeds remaining bytes");
    }
    const std::string_view value_view =
        cursor.substr(0, static_cast<size_t>(value_size));
    cursor.remove_prefix(static_cast<size_t>(value_size));

    // The record ends here; verify the CRC over exactly this span (timestamp
    // through value_bytes) before trusting any field.
    const size_t record_size = record.size() - cursor.size();
    const std::string_view covered =
        record.substr(kCrcCoveredOffset, record_size - kCrcCoveredOffset);
    if (mask(value(covered.data(), covered.size())) != stored_crc) {
        return Status::corruption("data record crc mismatch");
    }

    // Populate the result only after full validation.
    result->key = key;
    result->value = value_view;
    result->type = static_cast<ValueType>(type_byte);
    result->flag = static_cast<ValueFlag>(flag_byte);
    result->timestamp = timestamp;

    input->remove_prefix(record_size);
    return Status::ok();
}

Status decode_data_record(std::string_view input, ParsedDataRecord *result) {
    return decode_data_record(&input, result);
}

// Status data_record_size(std::string_view input, size_t *total_size,
//                         bool *complete) {
//     if (total_size == nullptr || complete == nullptr) {
//         return Status::invalid_argument("data_record_size: null output");
//     }
//     *complete = false;
//     *total_size = kDataRecordHeaderSize;

//     if (input.size() < kDataRecordHeaderSize) { return Status::ok(); }
//     if (decode_fixed_32(input.data()) != kDataRecordMagic) {
//         return Status::corruption("data record bad magic number");
//     }

//     const size_t key_start = kDataRecordHeaderSize;
//     std::string_view rest = input.substr(key_start);
//     uint64_t key_size = 0;
//     size_t key_varint_len = 0;
//     {
//         std::string_view before = rest;
//         if (!get_varint_64(&rest, &key_size)) {
//             *total_size = key_start + 10; // max varint64 width
//             return Status::ok();
//         }
//         key_varint_len = static_cast<size_t>(rest.data() - before.data());
//     }
//     if (key_size > UINT16_MAX) {
//         return Status::corruption("data record key_size implausibly large");
//     }

//     const size_t after_key =
//         key_start + key_varint_len + static_cast<size_t>(key_size);
//     if (input.size() < after_key + 1) {
//         *total_size = after_key + 10;
//         return Status::ok();
//     }

//     std::string_view vc = input.substr(after_key);
//     uint64_t value_size = 0;
//     size_t value_varint_len = 0;
//     {
//         std::string_view before = vc;
//         if (!get_varint_64(&vc, &value_size)) {
//             *total_size = after_key + 10;
//             return Status::ok();
//         }
//         value_varint_len = static_cast<size_t>(vc.data() - before.data());
//     }
//     if (value_size > (1ull << 40)) {
//         return Status::corruption("data record value_size implausibly
//         large");
//     }

//     *total_size = after_key + value_varint_len + kDataRecordValueMetaSize +
//                   static_cast<size_t>(value_size);
//     *complete = true;
//     return Status::ok();
// }

} // namespace ANCHORDB_NAMESPACE
