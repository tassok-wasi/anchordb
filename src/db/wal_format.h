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

#include <cstddef>
#include <cstdint>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

// AnchorDB write-ahead log on-disk format (format version 1).
//
// An 8-byte file header is followed by a run of frames.  A logical record (one
// atomic WAL entry) is stored in one or more frames; records larger than the
// frame payload budget are split, and every fragment is independently
// checksummed so a partial write is detectable.
//
// File header (8 bytes, once at offset 0):
//   +-------------------+------------------+-------------------+
//   | file_magic (4B LE)| version (2B LE)  | reserved (2B LE)  |
//   +-------------------+------------------+-------------------+
//
// Frame (11-byte header followed by `payload_len` payload bytes):
//   +-------------------+------------------+--------+---------------+---------+
//   | frame_magic (2B)  | payload_len (4B) | flags  | crc32 (4B LE) | payload |
//   +-------------------+------------------+--------+---------------+---------+
//     'A''N' on disk      LE, bounded        (1B)     raw CRC32       N bytes
//
//   - flags marks whether a frame starts and/or ends a logical record.
//   - crc32 is the raw (unmasked) CRC32 of the header bytes
//     `frame_magic | payload_len | flags` followed by the payload.
// ---------------------------------------------------------------------------

// 'A''N''D''W' on disk (little-endian encoding of this value).
inline constexpr uint32_t kWALFileMagic = 0x57444E41u;
inline constexpr uint16_t kWALFormatVersion = 1;
inline constexpr size_t kWALFileHeaderSize = 8;

// 'A''N' on disk.
inline constexpr uint16_t kWALFrameMagic = 0x4E41u;
inline constexpr size_t kWALFrameHeaderSize = 11;

// Default upper bound on a single frame's payload.  It is also the
// fragmentation threshold: logical records larger than this are split across
// multiple frames.
inline constexpr size_t kWALDefaultMaxFramePayload = 1u << 20; // 1 MiB

// Frame flag bits.
//
//   first | last  -> a complete record in one frame
//   first         -> the opening fragment of a split record
//   (none)        -> a middle fragment of a split record
//   last          -> the closing fragment of a split record
inline constexpr uint8_t kWALFlagFirstFragment = 0x01;
inline constexpr uint8_t kWALFlagLastFragment = 0x02;
inline constexpr uint8_t kWALKnownFlags =
    kWALFlagFirstFragment | kWALFlagLastFragment;

} // namespace ANCHORDB_NAMESPACE
