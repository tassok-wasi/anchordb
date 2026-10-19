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
#include "db/db_format.h"
#include "util/coding.h"
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ANCHORDB_NAMESPACE {

// A single index-block segment is exactly 64KB and is meant to be written to a
// 64KB-aligned boundary.  All intra-block offsets therefore fit in 16 bits:
// the byte offset of a payload entry is at most kDefaultBlockSize - 1.
inline constexpr int kDefaultBlockSize = 65536; // 64KB

// Fixed on-disk sizes of the index block layout below.
inline constexpr size_t kIndexHeaderSize = 4;
inline constexpr size_t kIndexSlotSize = 16;

// Byte offsets of the fields within one forward slot.
inline constexpr size_t kSlotKeyOffsetOff = 0;    // key_local_offset (2B)
inline constexpr size_t kSlotTypeOff = 2;         // value_type (1B)
inline constexpr size_t kSlotFlagOff = 3;         // value_flag (1B)
inline constexpr size_t kSlotDataSizeOff = 4;     // data_size (4B)
inline constexpr size_t kSlotGlobalOffsetOff = 8; // data_global_offset (8B)

struct ValueHandle {
    ValueType type;
    ValueFlag flag;
    uint64_t offset;
    // Total encoded size (in bytes) of the referenced record in data.bin.  This
    // is uint32_t rather than uint64_t because oversized values are pushed to
    // the vLog, so the index only ever points at a small inline record or a
    // small blob handle; the record's layout size always fits in 32 bits.
    uint32_t data_size;
};

// Serializes an index block into a fixed 64KB, cache-friendly segment.
//
// Layout (little-endian):
//   [header: slot_count(2B) | free_space_boundary(2B)]
//   [forward: fixed 16B slots, one per entry]
//       slot: key_local_offset(2B) | value_type(1B) | value_flag(1B) |
//             data_size(4B) | data_global_offset(8B)
//   [backward: variable-length payload, growing down from the end of the block]
//       payload: key_length(2B) | key_bytes | timestamp(8B)
//
// All offsets are local to the segment.  A free_space_boundary of 0 with
// slot_count 0 denotes an empty block; the sentinel is required because the
// empty-block boundary (65536) is not representable in 16 bits and never
// occurs once at least one entry has been stored.
class IndexBlockWriter {
  public:
    IndexBlockWriter() : buffer_(new char[kDefaultBlockSize]) {}

    IndexBlockWriter(const IndexBlockWriter &) = delete;
    IndexBlockWriter &operator=(const IndexBlockWriter &) = delete;

    ~IndexBlockWriter() { delete[] buffer_; }

    void reset() noexcept {
        slot_count_ = 0;
        free_space_boundary_ = kDefaultBlockSize;
        last_entry_offset_ = -1;
        sealed_ = false;
        finished_ = false;
    }

    // Appends one entry.  Returns true if the entry was stored, or false if
    // the block is full/sealed or the key is too large to encode.  A false
    // return means the caller must emit this block and start a new one.
    bool add(std::string_view key, ValueHandle handle, uint64_t timestamp);

    bool sealed() const noexcept { return sealed_; }
    bool finished() const noexcept { return finished_; }

    // Seals the block, writes the header and returns the full 64KB segment.
    // Idempotent: safe to call more than once.
    std::string_view finish() noexcept {
        write_header();
        sealed_ = true;
        finished_ = true;
        return std::string_view(buffer_,
                                static_cast<size_t>(kDefaultBlockSize));
    }

    static constexpr size_t block_size() noexcept {
        return static_cast<size_t>(kDefaultBlockSize);
    }

    bool empty() const noexcept { return slot_count_ == 0; }

    // Greatest key stored in this block (the last successfully added key), or
    // an empty view if no entry was stored.  Returned as a zero-copy view into
    // this writer's buffer, so it is valid until reset()/destruction.
    std::string_view last_key() const noexcept {
        if (last_entry_offset_ < 0) { return std::string_view(); }
        const char *entry = buffer_ + last_entry_offset_;
        const uint16_t key_len = decode_fixed_16(entry);
        return std::string_view(entry + sizeof(uint16_t), key_len);
    }

    // Byte offset of the last stored entry within the segment, or -1 if empty.
    int last_entry_offset() const noexcept { return last_entry_offset_; }

  private:
    // Encodes the header.  Must be called only when slot_count_ is final.
    void write_header() noexcept;

    char *buffer_;
    uint16_t slot_count_{0};
    int free_space_boundary_{kDefaultBlockSize};
    int last_entry_offset_{-1};
    bool sealed_{false};
    bool finished_{false};
};

} // namespace ANCHORDB_NAMESPACE
