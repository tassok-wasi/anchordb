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
#include "db/wal_format.h"
#include <cstdint>
#include <string>
namespace anchordb {

class SequentialFileReader;

class WALReader {
  public:
    class Reporter {
      public:
        virtual ~Reporter();
        virtual void corruption(size_t bytes, const Status &status) = 0;
    };

    WALReader(SequentialFileReader *file, Reporter *reporter, bool checksum,
              uint64_t initial_offset);

    WALReader(const WALReader &) = delete;
    WALReader &operator=(const WALReader &) = delete;

    ~WALReader();

    bool read_record(Slice *record, std::string *scratch);

    uint64_t last_record_offset();

  private:
    enum {
        kEOF = k_max_wal_record_type + 1,
        kBadRecord = k_max_wal_record_type + 2
    };

    bool skip_to_initial_block();

    unsigned int read_physical_record(Slice *result);

    void report_corruption(uint64_t bytes, const char *reason);
    void report_drop(uint64_t bytes, const Status &reason);

    SequentialFileReader *const file_;
    Reporter *const reporter_;
    bool const checksum_;
    char *const backing_store_;
    Slice buffer_;
    bool eof_; // Last read() indicated EOF by returning < k_wal_block_size

    // Offset of the last record returned by read_record.
    uint64_t last_record_offset_;
    // Offset of the first location past the end of buffer_.
    uint64_t end_of_buffer_offset_;

    // Offset at which to start looking for the first record to return
    const uint64_t initial_offset_;

    // True if we are resynchronizing after a seek (initial_offset_ > 0). In
    // particular, a run of kMiddleType and kLastType records can be silently
    // skipped in this mode
    bool resyncing_;
};
} // namespace anchordb
