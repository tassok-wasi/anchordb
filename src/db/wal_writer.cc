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

#include "db/wal_writer.h"

#include "anchordb/env.h"
#include "anchordb/status.h"
#include "db/wal_format.h"
#include "util/coding.h"
#include "util/crc32.h"
#include <algorithm>
#include <cstdint>
#include <string_view>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

WALWriter::WALWriter(WritableFileWriter *dest, size_t max_frame_payload)
    : dest_(dest), max_frame_payload_(max_frame_payload) {
    // The frame length field is 32-bit; a zero budget would make fragmentation
    // unable to make progress.
    if (max_frame_payload_ == 0) { max_frame_payload_ = 1; }
    if (max_frame_payload_ > UINT32_MAX) { max_frame_payload_ = UINT32_MAX; }

    // Write the file header up front so even an empty log is self-identifying.
    char header[kWALFileHeaderSize];
    encode_fixed_32(header, kWALFileMagic);
    encode_fixed_16(header + 4, kWALFormatVersion);
    encode_fixed_16(header + 6, 0); // reserved
    Status s = dest_->append(std::string_view(header, kWALFileHeaderSize));
    if (!s.is_ok()) {
        status_ = s;
    } else {
        bytes_written_ += kWALFileHeaderSize;
    }
}

WALWriter::~WALWriter() = default;

Status WALWriter::append_frame_(uint8_t flags, std::string_view payload) {
    // Header: frame_magic(2B) | payload_len(4B) | flags(1B) | crc32(4B).
    char header[kWALFrameHeaderSize];
    encode_fixed_16(header, kWALFrameMagic);
    encode_fixed_32(header + 2, static_cast<uint32_t>(payload.size()));
    header[6] = static_cast<char>(flags);

    // CRC over the seven header bytes then the payload.
    uint32_t crc = value(header, 7);
    if (!payload.empty()) { crc = extend(crc, payload.data(), payload.size()); }
    encode_fixed_32(header + 7, crc);

    Status s = dest_->append(std::string_view(header, kWALFrameHeaderSize));
    if (!s.is_ok()) {
        status_ = s;
        return status_;
    }
    if (!payload.empty()) {
        s = dest_->append(payload);
        if (!s.is_ok()) {
            status_ = s;
            return status_;
        }
    }

    bytes_written_ += kWALFrameHeaderSize + payload.size();
    return Status::ok();
}

Status WALWriter::append(std::string_view record) {
    if (!status_.is_ok()) { return status_; }

    // Small enough to fit in a single frame.
    if (record.size() <= max_frame_payload_) {
        return append_frame_(kWALFlagFirstFragment | kWALFlagLastFragment,
                             record);
    }

    // Split a large logical record into first / middle* / last fragments.
    size_t offset = 0;
    bool first = true;
    while (offset < record.size()) {
        const size_t n = std::min(max_frame_payload_, record.size() - offset);
        const bool last = (offset + n >= record.size());
        uint8_t flags = 0;
        if (first) { flags |= kWALFlagFirstFragment; }
        if (last) { flags |= kWALFlagLastFragment; }
        Status s = append_frame_(flags, record.substr(offset, n));
        if (!s.is_ok()) { return s; }
        offset += n;
        first = false;
    }
    return Status::ok();
}

Status WALWriter::flush() {
    if (!status_.is_ok()) { return status_; }
    Status s = dest_->flush();
    if (!s.is_ok()) { status_ = s; }
    return status_;
}

Status WALWriter::sync() {
    if (!status_.is_ok()) { return status_; }
    Status s = flush();
    if (!s.is_ok()) { return s; }
    s = dest_->sync();
    if (!s.is_ok()) { status_ = s; }
    return status_;
}

} // namespace ANCHORDB_NAMESPACE
