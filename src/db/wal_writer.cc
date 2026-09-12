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
#include "db/wal_format.h"
#include "db/wal_writer.h"
#include "util/coding.h"
#include "util/crc32.h"
namespace anchordb {

namespace {
void init_type_crc(uint32_t *type_crc) {
    for (int i = 0; i <= k_max_wal_record_type; i++) {
        char t = static_cast<char>(i);
        type_crc[i] = value(&t, 1);
    }
}
} // namespace

WALWriter::WALWriter(WritableFileWriter *dest) : dest_(dest), block_offset_(0) {
    init_type_crc(type_crc_);
}

WALWriter::WALWriter(WritableFileWriter *dest, uint64_t dest_length)
    : dest_(dest), block_offset_(dest_length % k_wal_block_size) {
    init_type_crc(type_crc_);
}

WALWriter::~WALWriter() = default;

Status WALWriter::add_record(const Slice &slice) {
    const char *ptr = slice.data();
    size_t left = slice.size();

    // Fragment the record if necessary and emit it.  Note that if slice
    // is empty, we still want to iterate once to emit a single
    // zero-length record
    Status s;
    bool begin = true;
    do {
        const int leftover = k_wal_block_size - block_offset_;
        assert(leftover >= 0);
        if (leftover < k_wal_header_size) {
            // Switch to a new block
            if (leftover > 0) {
                // Fill the trailer (literal below relies on k_wal_header_size
                // being 7)
                static_assert(k_wal_header_size == 7, "");
                dest_->append(Slice("\x00\x00\x00\x00\x00\x00", leftover));
            }
            block_offset_ = 0;
        }

        // Invariant: we never leave < k_wal_header_size bytes in a block.
        assert(k_wal_block_size - block_offset_ - k_wal_header_size >= 0);

        const size_t avail =
            k_wal_block_size - block_offset_ - k_wal_header_size;
        const size_t fragment_length = (left < avail) ? left : avail;

        WALRecordType type;
        const bool end = (left == fragment_length);
        if (begin && end) {
            type = kFullType;
        } else if (begin) {
            type = kFirstType;
        } else if (end) {
            type = kLastType;
        } else {
            type = kMiddleType;
        }

        s = emit_physical_record(type, ptr, fragment_length);
        ptr += fragment_length;
        left -= fragment_length;
        begin = false;
    } while (s.is_ok() && left > 0);
    return s;
}

Status WALWriter::emit_physical_record(WALRecordType t, const char *ptr,
                                       size_t length) {
    assert(length <= 0xffff); // Must fit in two bytes
    assert(block_offset_ + k_wal_header_size + length <= k_wal_block_size);

    // Format the header
    char buf[k_wal_header_size];
    buf[4] = static_cast<char>(length & 0xff);
    buf[5] = static_cast<char>(length >> 8);
    buf[6] = static_cast<char>(t);

    // Compute the crc of the record type and the payload.
    uint32_t crc = extend(type_crc_[t], ptr, length);
    crc = mask(crc); // Adjust for storage
    encode_fixed_32(buf, crc);

    // Write the header and the payload
    Status s = dest_->append(Slice(buf, k_wal_header_size));
    if (s.is_ok()) {
        s = dest_->append(Slice(ptr, length));
        if (s.is_ok()) {
            s = dest_->flush();
        }
    }
    block_offset_ += k_wal_header_size + length;
    return s;
}

} // namespace anchordb
