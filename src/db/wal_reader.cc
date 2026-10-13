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

#include "anchordb/env.h"
#include "anchordb/status.h"
#include "db/wal_reader.h"
#include "util/coding.h"
#include "util/crc32.h"
#include <cstdint>

namespace anchordb {

WALReader::WALReader(SequentialFileReader *reader) : reader_(reader) {}

bool WALReader::read_record(std::string_view *record, std::string *scratch) {
    // Read the 4-byte masked CRC
    char crc_buf[4];
    std::string_view crc_slice;
    Status s = reader_->read(4, &crc_slice, crc_buf);
    if (!s.is_ok() || crc_slice.size() < 4) {
        return false; // Clean EOF or truncated header
    }

    uint32_t stored_crc = decode_fixed_32(crc_slice.data());

    // Read the Varint32 payload length byte-by-byte
    uint32_t length = 0;
    int shift = 0;
    for (int i = 0; i < 5; ++i) {
        std::string_view byte_slice;
        char b;
        s = reader_->read(1, &byte_slice, &b);
        if (!s.is_ok() || byte_slice.empty()) {
            return false; // Truncated varint
        }
        uint8_t val = static_cast<uint8_t>(byte_slice[0]);
        length |= static_cast<uint8_t>(val & 0x7F) << shift;
        if (!(val & 0x80)) {
            break;
        }
        shift += 7;
    }

    // Read the exact payload into scratch
    scratch->resize(length);
    std::string_view payload_slice;
    s = reader_->read(length, &payload_slice, &(*scratch)[0]);
    if (!s.is_ok() || payload_slice.size() != length) {
        return false; // Truncated payload (crash cutoff)
    }

    // Verify CRC integrity
    uint32_t calculated_crc = value(payload_slice.data(), payload_slice.size());
    calculated_crc = mask(calculated_crc);

    if (calculated_crc != stored_crc) {
        // Mid-file corruption or bit-rot detected
        return false;
    }

    *record = payload_slice;
    return true;
}

} // namespace anchordb
