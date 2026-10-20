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

#include "anchordb/status.h"
#include "db/wal_format.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

class SequentialFileReader;

// Reads the framed record stream written by WALWriter (see db/wal_format.h for
// the on-disk format).
//
// On the first read the reader consumes and validates the file header, then
// returns one logical record at a time, transparently reassembling fragments
// and verifying every frame's checksum.  The caller supplies a reusable
// `scratch` buffer; for a single-frame record `*record` views into `scratch`,
// and for a reassembled record it views into an internal buffer, so `*record`
// is valid only until the next call to read_record().
//
// Return contract:
//   * ok, *eof == false, *record set : one record was read.
//   * ok, *eof == true               : clean end of log.  A trailing partial
//                                      frame (a torn write from a crash) is
//                                      treated as end of log, not an error.
//   * non-ok                         : corruption or I/O error; *eof == false.
//
// Not thread-safe: use a single reader per file.
class WALReader {
  public:
    // `max_frame_payload` must be at least the writer's value; a frame claiming
    // more is rejected as corruption before any read.  Zero is clamped up to
    // one byte and values above UINT32_MAX are clamped down.
    explicit WALReader(SequentialFileReader *reader,
                       size_t max_frame_payload = kWALDefaultMaxFramePayload,
                       bool verify_checksums = true);

    WALReader(const WALReader &) = delete;
    WALReader &operator=(const WALReader &) = delete;

    ~WALReader();

    // Reads the next logical record.  See the class comment for the return
    // contract.
    Status read_record(std::string_view *record, bool *eof,
                       std::string *scratch);

    // Number of physical bytes consumed from the file so far.  Best-effort
    // diagnostic metric.
    uint64_t bytes_read() const { return bytes_read_; }

  private:
    // Consumes and validates the file header.  Sets *empty when the file holds
    // no bytes at all (an empty log is valid).
    Status read_file_header_(bool *empty);

    // Reads and validates one frame.  On clean/partial end of log sets *eof and
    // returns ok; on corruption or I/O error returns non-ok.
    Status read_frame_(uint8_t *flags, std::string_view *payload, bool *eof,
                       std::string *scratch);

    // Clears any in-progress reassembly and returns a corruption status.
    Status abort_fragment_(std::string_view msg);

    SequentialFileReader *reader_;
    size_t max_frame_payload_;
    bool verify_checksums_;
    bool header_read_ = false;
    // Reassembly buffer for fragmented logical records.
    std::string fragmented_;
    bool in_fragment_ = false;
    uint64_t bytes_read_ = 0;
};

} // namespace ANCHORDB_NAMESPACE
