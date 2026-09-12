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
#include "anchordb/slice.h"
#include "anchordb/status.h"
#include "db/wal_format.h"
#include "db/wal_reader.h"
#include "util/coding.h"
#include "util/crc32.h"
#include <cstdint>
namespace anchordb {

WALReader::Reporter::~Reporter() = default;

WALReader::WALReader(SequentialFileReader *file, Reporter *reporter,
                     bool checksum, uint64_t initial_offset)
    : file_(file), reporter_(reporter), checksum_(checksum),
      backing_store_(new char[k_wal_block_size]), buffer_(), eof_(false),
      last_record_offset_(0), end_of_buffer_offset_(0),
      initial_offset_(initial_offset), resyncing_(initial_offset > 0) {}

WALReader::~WALReader() { delete[] backing_store_; }

bool WALReader::skip_to_initial_block() {
    const size_t offset_in_block = initial_offset_ % k_wal_block_size;
    uint64_t block_start_location = initial_offset_ - offset_in_block;

    // Don't search a block if we'd be in the trailer
    if (offset_in_block > k_wal_block_size - 6) {
        block_start_location += k_wal_block_size;
    }

    end_of_buffer_offset_ = block_start_location;

    // Skip to start of first block that can contain the initial record
    if (block_start_location > 0) {
        Status skip_status = file_->skip(block_start_location);
        if (!skip_status.is_ok()) {
            report_drop(block_start_location, skip_status);
            return false;
        }
    }

    return true;
}

bool WALReader::read_record(Slice *record, std::string *scratch) {
    if (last_record_offset_ < initial_offset_) {
        if (!skip_to_initial_block()) {
            return false;
        }
    }

    scratch->clear();
    record->clear();
    bool in_fragmented_record = false;
    // Record offset of the logical record that we're reading
    // 0 is a dummy value to make compilers happy
    uint64_t prospective_record_offset = 0;

    Slice fragment;
    while (true) {
        const unsigned int record_type = read_physical_record(&fragment);

        // ReadPhysicalRecord may have only had an empty trailer remaining in
        // its internal buffer. Calculate the offset of the next physical record
        // now that it has returned, properly accounting for its header size.
        uint64_t physical_record_offset = end_of_buffer_offset_ -
                                          buffer_.size() - k_wal_header_size -
                                          fragment.size();

        if (resyncing_) {
            if (record_type == kMiddleType) {
                continue;
            } else if (record_type == kLastType) {
                resyncing_ = false;
                continue;
            } else {
                resyncing_ = false;
            }
        }

        switch (record_type) {
        case kFullType:
            if (in_fragmented_record) {
                if (!scratch->empty()) {
                    report_corruption(scratch->size(),
                                      "partial record without end(1)");
                }
            }
            prospective_record_offset = physical_record_offset;
            scratch->clear();
            *record = fragment;
            last_record_offset_ = prospective_record_offset;
            return true;

        case kFirstType:
            if (in_fragmented_record) {
                if (!scratch->empty()) {
                    report_corruption(scratch->size(),
                                      "partial record without end(2)");
                }
            }
            prospective_record_offset = physical_record_offset;
            scratch->assign(fragment.data(), fragment.size());
            in_fragmented_record = true;
            break;

        case kMiddleType:
            if (!in_fragmented_record) {
                report_corruption(fragment.size(),
                                  "missing start of fragmented record(1)");
            } else {
                scratch->append(fragment.data(), fragment.size());
            }
            break;

        case kLastType:
            if (!in_fragmented_record) {
                report_corruption(fragment.size(),
                                  "missing start of fragmented record(2)");
            } else {
                scratch->append(fragment.data(), fragment.size());
                *record = Slice(*scratch);
                last_record_offset_ = prospective_record_offset;
                return true;
            }
            break;

        case kEOF:
            if (in_fragmented_record) {
                // This can be caused by the writer dying immediately after
                // writing a physical record but before completing the next;
                // don't treat it as a corruption, just ignore the entire
                // logical record.
                scratch->clear();
            }
            return false;

        case kBadRecord:
            if (in_fragmented_record) {
                report_corruption(scratch->size(), "error in middle of record");
                in_fragmented_record = false;
                scratch->clear();
            }
            break;

        default: {
            char buf[40];
            std::snprintf(buf, sizeof(buf), "unknown record type %u",
                          record_type);
            report_corruption((fragment.size() +
                               (in_fragmented_record ? scratch->size() : 0)),
                              buf);
            in_fragmented_record = false;
            scratch->clear();
            break;
        }
        }
    }
    return false;
}

uint64_t WALReader::last_record_offset() { return last_record_offset_; }

void WALReader::report_corruption(uint64_t bytes, const char *reason) {
    report_drop(bytes, Status::corruption(reason));
}

void WALReader::report_drop(uint64_t bytes, const Status &reason) {
    if (reporter_ != nullptr &&
        end_of_buffer_offset_ - buffer_.size() - bytes >= initial_offset_) {
        reporter_->corruption(static_cast<size_t>(bytes), reason);
    }
}

unsigned int WALReader::read_physical_record(Slice *result) {
    while (true) {
        if (buffer_.size() < k_wal_header_size) {
            if (!eof_) {
                // Last read was a full read, so this is a trailer to skip
                buffer_.clear();
                Status status =
                    file_->read(k_wal_block_size, &buffer_, backing_store_);
                end_of_buffer_offset_ += buffer_.size();
                if (!status.is_ok()) {
                    buffer_.clear();
                    report_drop(k_wal_block_size, status);
                    eof_ = true;
                    return kEOF;
                } else if (buffer_.size() < k_wal_block_size) {
                    eof_ = true;
                }
                continue;
            } else {
                // Note that if buffer_ is non-empty, we have a truncated header
                // at the end of the file, which can be caused by the writer
                // crashing in the middle of writing the header. Instead of
                // considering this an error, just report EOF.
                buffer_.clear();
                return kEOF;
            }
        }

        // Parse the header
        const char *header = buffer_.data();
        const uint32_t a = static_cast<uint32_t>(header[4]) & 0xff;
        const uint32_t b = static_cast<uint32_t>(header[5]) & 0xff;
        const unsigned int type = header[6];
        const uint32_t length = a | (b << 8);
        if (k_wal_header_size + length > buffer_.size()) {
            size_t drop_size = buffer_.size();
            buffer_.clear();
            if (!eof_) {
                report_corruption(drop_size, "bad record length");
                return kBadRecord;
            }
            // If the end of the file has been reached without reading |length|
            // bytes of payload, assume the writer died in the middle of writing
            // the record. Don't report a corruption.
            return kEOF;
        }

        if (type == kZeroType && length == 0) {
            // Skip zero length record without reporting any drops since
            // such records are produced by the mmap based writing code in
            // env_posix.cc that preallocates file regions.
            buffer_.clear();
            return kBadRecord;
        }

        // Check crc
        if (checksum_) {
            uint32_t expected_crc = unmask(decode_fixed_32(header));
            uint32_t actual_crc = value(header + 6, 1 + length);
            if (actual_crc != expected_crc) {
                // Drop the rest of the buffer since "length" itself may have
                // been corrupted and if we trust it, we could find some
                // fragment of a real log record that just happens to look
                // like a valid log record.
                size_t drop_size = buffer_.size();
                buffer_.clear();
                report_corruption(drop_size, "checksum mismatch");
                return kBadRecord;
            }
        }

        buffer_.remove_prefix(k_wal_header_size + length);

        // Skip physical record that started before initial_offset_
        if (end_of_buffer_offset_ - buffer_.size() - k_wal_header_size -
                length <
            initial_offset_) {
            result->clear();
            return kBadRecord;
        }

        *result = Slice(header + k_wal_header_size, length);
        return type;
    }
}

} // namespace anchordb
