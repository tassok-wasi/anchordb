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

#include "db/wal_reader.h"

#include "anchordb/env.h"
#include "anchordb/status.h"
#include "db/wal_format.h"
#include "util/coding.h"
#include "util/crc32.h"
#include <cstdint>
#include <string>
#include <string_view>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

WALReader::WALReader(SequentialFileReader *reader, size_t max_frame_payload,
                     bool verify_checksums)
    : reader_(reader), max_frame_payload_(max_frame_payload),
      verify_checksums_(verify_checksums) {
    if (max_frame_payload_ == 0) { max_frame_payload_ = 1; }
    if (max_frame_payload_ > UINT32_MAX) { max_frame_payload_ = UINT32_MAX; }
}

WALReader::~WALReader() = default;

Status WALReader::abort_fragment_(std::string_view msg) {
    in_fragment_ = false;
    fragmented_.clear();
    return Status::corruption(msg);
}

Status WALReader::read_file_header_(bool *empty) {
    *empty = false;

    char header[kWALFileHeaderSize];
    std::string_view view;
    Status s = reader_->read(kWALFileHeaderSize, &view, header);
    if (!s.is_ok()) { return s; }
    bytes_read_ += view.size();

    if (view.size() == 0) {
        // No bytes at all: an empty log is valid.
        *empty = true;
        return Status::ok();
    }
    if (view.size() < kWALFileHeaderSize) {
        return Status::corruption("wal: truncated file header");
    }
    if (decode_fixed_32(view.data()) != kWALFileMagic) {
        return Status::corruption("wal: bad file magic");
    }
    if (decode_fixed_16(view.data() + 4) != kWALFormatVersion) {
        return Status::corruption("wal: unsupported format version");
    }
    return Status::ok();
}

Status WALReader::read_frame_(uint8_t *flags, std::string_view *payload,
                              bool *eof, std::string *scratch) {
    *payload = std::string_view();
    *eof = false;

    // Read the fixed-size frame header.  SequentialFileReader::read copies into
    // the caller's buffer and reports a short count only at end of file.
    char header[kWALFrameHeaderSize];
    std::string_view hview;
    Status s = reader_->read(kWALFrameHeaderSize, &hview, header);
    if (!s.is_ok()) { return s; }
    bytes_read_ += hview.size();

    if (hview.size() == 0) {
        // Clean end of log.
        *eof = true;
        return Status::ok();
    }
    if (hview.size() < kWALFrameHeaderSize) {
        // Torn header at the tail: a crash cut the write short.  Treat as end
        // of log; the incomplete record is discarded.
        *eof = true;
        return Status::ok();
    }

    if (decode_fixed_16(hview.data()) != kWALFrameMagic) {
        return Status::corruption("wal: bad frame magic");
    }
    const uint32_t length = decode_fixed_32(hview.data() + 2);
    const uint8_t frame_flags = static_cast<uint8_t>(hview.data()[6]);
    const uint32_t stored_crc = decode_fixed_32(hview.data() + 7);

    if ((frame_flags & ~kWALKnownFlags) != 0) {
        return Status::corruption("wal: unknown frame flags",
                                  std::to_string(frame_flags));
    }
    if (length > max_frame_payload_) {
        return Status::corruption("wal: frame length exceeds maximum",
                                  std::to_string(length));
    }

    std::string_view pview;
    if (length > 0) {
        scratch->resize(length);
        s = reader_->read(length, &pview, scratch->data());
        if (!s.is_ok()) { return s; }
        bytes_read_ += pview.size();
        if (pview.size() != length) {
            // Torn payload at the tail: the header was complete but the payload
            // was cut short by a crash.  Treat as end of log.
            *eof = true;
            return Status::ok();
        }
    }

    if (verify_checksums_) {
        uint32_t crc = value(hview.data(), 7);
        if (length > 0) { crc = extend(crc, pview.data(), pview.size()); }
        if (crc != stored_crc) {
            return Status::corruption("wal: frame checksum mismatch");
        }
    }

    *flags = frame_flags;
    *payload = pview;
    return Status::ok();
}

Status WALReader::read_record(std::string_view *record, bool *eof,
                              std::string *scratch) {
    *record = std::string_view();
    *eof = false;

    if (!header_read_) {
        bool empty = false;
        Status s = read_file_header_(&empty);
        if (!s.is_ok()) { return s; }
        header_read_ = true;
        if (empty) {
            *eof = true;
            return Status::ok();
        }
    }

    while (true) {
        uint8_t flags = 0;
        std::string_view payload;
        bool frame_eof = false;
        Status s = read_frame_(&flags, &payload, &frame_eof, scratch);
        if (!s.is_ok()) { return s; }
        if (frame_eof) {
            if (in_fragment_) {
                return abort_fragment_("wal: truncated fragmented record");
            }
            *eof = true;
            return Status::ok();
        }

        const bool first = (flags & kWALFlagFirstFragment) != 0;
        const bool last = (flags & kWALFlagLastFragment) != 0;

        if (first && last) {
            // Whole record in a single frame: hand back the scratch view with
            // no extra copy.
            if (in_fragment_) {
                return abort_fragment_(
                    "wal: new record started mid-fragmentation");
            }
            *record = payload;
            return Status::ok();
        }

        if (first) {
            if (in_fragment_) {
                return abort_fragment_(
                    "wal: new record started mid-fragmentation");
            }
            fragmented_.clear();
            if (!payload.empty()) {
                fragmented_.append(payload.data(), payload.size());
            }
            in_fragment_ = true;
            continue;
        }

        if (!in_fragment_) {
            return Status::corruption(
                "wal: continuation frame without an opening fragment");
        }
        if (!payload.empty()) {
            fragmented_.append(payload.data(), payload.size());
        }
        if (last) {
            in_fragment_ = false;
            *record = std::string_view(fragmented_.data(), fragmented_.size());
            return Status::ok();
        }
    }
}

} // namespace ANCHORDB_NAMESPACE
