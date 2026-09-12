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

#include "anchordb/slice.h"
#include "anchordb/status.h"
#include "wal_format.h"
#include <cstdint>
namespace anchordb {

class WritableFileWriter;

class WALWriter {
  public:
    explicit WALWriter(WritableFileWriter *dest);

    WALWriter(WritableFileWriter *dest, uint64_t dest_len);

    WALWriter(const WALWriter &) = delete;
    WALWriter &operator=(const WALWriter &) = delete;

    ~WALWriter();

    Status add_record(const Slice &slice);

  private:
    Status emit_physical_record(WALRecordType type, const char *ptr,
                                size_t length);
    WritableFileWriter *dest_;
    int block_offset_;

    uint32_t type_crc_[k_max_wal_record_type + 1];
};

} // namespace anchordb
