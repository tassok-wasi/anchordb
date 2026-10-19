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
#include <cstdint>
#include <string>
#include <string_view>

namespace ANCHORDB_NAMESPACE {

class WritableFileWriter;

// Where a record landed inside a record file: the absolute byte offset and the
// total encoded size.  This is exactly the (offset, data_size) pair that an
// index slot or a blob handle records, so a caller can feed it straight into
// IndexTableBuilder or BlobHandle.
struct RecordLocation {
    uint64_t offset = 0;
    uint32_t size = 0;
};

// Append-only writer for the unified data-record format shared by data.bin and
// the WiscKey value log (see sstable/data_record.h).
//
// The writer does not own `dest`; the caller opens it through Env (create with
// new_writable_file_writer, or resume with new_appendable_file_writer) and
// keeps it alive.  The writer only tracks the running append offset and frames
// records, so it serves both lifecycles:
//   - data.bin: one-shot during a flush, seal_size == 0.
//   - vLog: long-lived and append-only; seal_size > 0 makes sealed() report
//     true once the file should be rolled, leaving the roll policy to the
//     caller.
class RecordFileWriter {
  public:
    // `initial_size` seeds the offset for an appendable (resumed) file; pass 0
    // for a freshly created file.  A `seal_size` of 0 disables sealing.
    RecordFileWriter(WritableFileWriter *dest, uint64_t initial_size = 0,
                     uint64_t seal_size = 0);

    RecordFileWriter(const RecordFileWriter &) = delete;
    RecordFileWriter &operator=(const RecordFileWriter &) = delete;

    // Encodes one record and appends it.  `value_bytes` is the raw inline value
    // when flag == inline_, or an already-encoded BlobHandle when flag == blob.
    // On success fills *out (if non-null) with the record's location.
    Status add(std::string_view key, std::string_view value_bytes,
               ValueType type, ValueFlag flag, uint64_t timestamp,
               RecordLocation *out);

    // Appends a record that has already been encoded (e.g. by
    // encode_data_record), for callers that already hold the bytes.  Returns
    // Status::invalid_argument if the record cannot fit in a 32-bit location.
    Status append(std::string_view encoded_record, RecordLocation *out);

    // Bytes appended so far (also the offset of the next record).
    uint64_t size() const { return size_; }

    // True once size() has reached the seal threshold.  Always false when
    // sealing is disabled.
    bool sealed() const { return seal_size_ != 0 && size_ >= seal_size_; }

    Status flush();
    Status sync();
    Status close();

  private:
    WritableFileWriter *dest_;
    uint64_t size_;
    uint64_t seal_size_;
    // Reused across add() calls so encoding does not allocate per record.
    std::string scratch_;
};

} // namespace ANCHORDB_NAMESPACE
