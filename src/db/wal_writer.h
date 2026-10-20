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
#include <string_view>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

class WritableFileWriter;

// Writes a stream of framed records to an AnchorDB write-ahead log (see
// db/wal_format.h for the on-disk format).
//
// The writer emits the file header when it is constructed, so it must be given
// a fresh (empty) file.  append() writes one logical record, splitting it into
// fragments when it exceeds `max_frame_payload`; every fragment is
// independently checksummed.  Call flush() (hand buffered bytes to the file
// layer) and/or sync() (flush + fsync) before acknowledging writes as durable.
//
// Errors are sticky: once append()/flush()/sync() fails, every later call
// returns the same Status without touching the file again.  This prevents a
// caller from unknowingly continuing to write a file that may be partially
// written after an I/O error.
//
// Not thread-safe: use a single writer per file, serialized by the caller.
class WALWriter {
  public:
    // `max_frame_payload` bounds a single frame's payload and doubles as the
    // fragmentation threshold.  Zero is clamped up to one byte, and values
    // above UINT32_MAX are clamped down (the frame length field is 32-bit).
    explicit WALWriter(WritableFileWriter *dest,
                       size_t max_frame_payload = kWALDefaultMaxFramePayload);

    WALWriter(const WALWriter &) = delete;
    WALWriter &operator=(const WALWriter &) = delete;

    ~WALWriter();

    // Appends one logical record.  The record may be empty.  Returns the sticky
    // status; if a previous operation failed this returns that error and does
    // not write anything.
    Status append(std::string_view record);

    // Flushes buffered bytes to the underlying file.
    Status flush();

    // Flushes and fsyncs the underlying file (makes prior appends durable).
    Status sync();

    // Sticky status of the writer: non-ok once any operation has failed.
    const Status &status() const { return status_; }

    // Number of physical bytes handed to the file so far (file header, frame
    // headers and payloads).  Best-effort diagnostic metric.
    uint64_t bytes_written() const { return bytes_written_; }

  private:
    // Encodes and appends one frame; updates the sticky status.
    Status append_frame_(uint8_t flags, std::string_view payload);

    WritableFileWriter *dest_;
    size_t max_frame_payload_;
    Status status_;
    uint64_t bytes_written_ = 0;
};

} // namespace ANCHORDB_NAMESPACE
