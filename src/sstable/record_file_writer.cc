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
#include "sstable/data_record.h"
#include "sstable/record_file_writer.h"
#include <cstdint>
#include <string_view>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

namespace {

// A record's location is stored as a uint32_t size, so reject anything larger
// up front rather than silently truncating it.
Status check_record_size(size_t encoded_len) {
    if (encoded_len > UINT32_MAX) {
        return Status::invalid_argument(
            "record_file_writer: record exceeds 32-bit size",
            std::to_string(encoded_len));
    }
    return Status::ok();
}

} // namespace

RecordFileWriter::RecordFileWriter(WritableFileWriter *dest,
                                   uint64_t initial_size, uint64_t seal_size)
    : dest_(dest), size_(initial_size), seal_size_(seal_size) {}

Status RecordFileWriter::add(std::string_view key, std::string_view value_bytes,
                             ValueType type, ValueFlag flag, uint64_t timestamp,
                             RecordLocation *out) {
    if (dest_ == nullptr) {
        return Status::invalid_argument("record_file_writer: null destination");
    }

    Status s = check_record_size(
        data_record_encoded_len(key.size(), value_bytes.size()));
    if (!s.is_ok()) { return s; }

    scratch_.clear();
    const size_t encoded_len =
        encode_data_record(&scratch_, timestamp, key, value_bytes, type, flag);
    return append(std::string_view(scratch_.data(), encoded_len), out);
}

Status RecordFileWriter::append(std::string_view encoded_record,
                                RecordLocation *out) {
    if (dest_ == nullptr) {
        return Status::invalid_argument("record_file_writer: null destination");
    }

    Status s = check_record_size(encoded_record.size());
    if (!s.is_ok()) { return s; }

    s = dest_->append(encoded_record);
    if (!s.is_ok()) { return s; }

    if (out != nullptr) {
        out->offset = size_;
        out->size = static_cast<uint32_t>(encoded_record.size());
    }
    size_ += encoded_record.size();
    return Status::ok();
}

Status RecordFileWriter::flush() {
    if (dest_ == nullptr) {
        return Status::invalid_argument("record_file_writer: null destination");
    }
    return dest_->flush();
}

Status RecordFileWriter::sync() {
    if (dest_ == nullptr) {
        return Status::invalid_argument("record_file_writer: null destination");
    }
    return dest_->sync();
}

Status RecordFileWriter::close() {
    if (dest_ == nullptr) {
        return Status::invalid_argument("record_file_writer: null destination");
    }
    return dest_->close();
}

} // namespace ANCHORDB_NAMESPACE
