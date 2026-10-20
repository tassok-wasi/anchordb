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
#include "file/file_meta.h"
#include "util/coding.h"
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ANCHORDB_NAMESPACE {

// Default index-block segment size.  The size is configurable per writer (see
// IndexBlockWriter's constructor); this is the value used when none is given.
inline constexpr size_t kDefaultBlockSize = 65536; // 64KB

// Index-block segment sizes are rounded up to a multiple of this value, so the
// uncompressed block size is always a 4KB multiple.  This is the decompressed
// size only: on-disk blocks may be compressed and need not be block-aligned.
inline constexpr size_t kIndexBlockAlignment = k_o_direct_alignment; // 4096

// Rounds `block_size` up to a kIndexBlockAlignment multiple, clamped to the
// largest size a 32-bit block offset can address.  Shared by the writer and
// reader so both agree on the segment size for a given configuration.
inline size_t round_index_block_size(size_t block_size) noexcept {
    if (block_size < kIndexBlockAlignment) {
        block_size = kIndexBlockAlignment;
    }
    const size_t aligned =
        ((block_size + kIndexBlockAlignment - 1) / kIndexBlockAlignment) *
        kIndexBlockAlignment;
    const size_t max_block_size =
        (static_cast<size_t>(UINT32_MAX) / kIndexBlockAlignment) *
        kIndexBlockAlignment;
    return aligned > max_block_size ? max_block_size : aligned;
}

// Fixed on-disk sizes of the index block layout below.
inline constexpr size_t kIndexHeaderSize = 8;
inline constexpr size_t kIndexSlotSize = 18;

// Byte offsets of the fields within one forward slot.
inline constexpr size_t kSlotKeyOffsetOff = 0;     // key_local_offset (4B)
inline constexpr size_t kSlotTypeOff = 4;          // value_type (1B)
inline constexpr size_t kSlotFlagOff = 5;          // value_flag (1B)
inline constexpr size_t kSlotDataSizeOff = 6;      // data_size (4B)
inline constexpr size_t kSlotGlobalOffsetOff = 10; // data_global_offset (8B)

struct ValueHandle {
    ValueType type;
    ValueFlag flag;
    uint64_t offset;
    // Total encoded size of the referenced record in data.bin.  uint32_t
    // because oversized values go to the vLog, so the record always fits in 32
    // bits.
    uint32_t data_size;
};

// Serializes an index block into a fixed-size, cache-friendly segment.
//
// Layout (little-endian):
//   [header: slot_count(4B) | free_space_boundary(4B)]
//   [forward: fixed 18B slots, one per entry]
//       slot: key_local_offset(4B) | value_type(1B) | value_flag(1B) |
//             data_size(4B) | data_global_offset(8B)
//   [backward: variable-length payload, growing down from the end of the block]
//       payload: key_length(4B) | key_bytes | timestamp(8B)
//
// All offsets are local to the segment and are stored as 32-bit values, so a
// segment may be configured larger than 64KB.  The segment size is rounded up
// to the nearest multiple of kIndexBlockAlignment by the constructor.
class IndexBlockWriter {
  public:
    // `block_size` is aligned via round_index_block_size().
    explicit IndexBlockWriter(size_t block_size = kDefaultBlockSize)
        : block_size_(round_index_block_size(block_size)),
          buffer_(new char[block_size_]), free_space_boundary_(block_size_) {}

    IndexBlockWriter(const IndexBlockWriter &) = delete;
    IndexBlockWriter &operator=(const IndexBlockWriter &) = delete;

    ~IndexBlockWriter() { delete[] buffer_; }

    void reset() noexcept {
        slot_count_ = 0;
        free_space_boundary_ = block_size_;
        last_entry_offset_ = -1;
        sealed_ = false;
        finished_ = false;
    }

    // Retargets the writer to a new segment size for the next block.  Only
    // valid while the current block has no un-emitted entries; otherwise false
    // is returned and the writer is untouched.  On success the block state is
    // cleared, so the next add() starts a fresh block.
    bool set_block_size(size_t block_size);

    // Appends one entry.  Returns true if the entry was stored, or false if
    // the block is full/sealed or the key is too large to encode.  A false
    // return means the caller must emit this block and start a new one.
    bool add(std::string_view key, ValueHandle handle, uint64_t timestamp);

    bool sealed() const noexcept { return sealed_; }
    bool finished() const noexcept { return finished_; }

    // Seals the block, writes the header and returns the full segment.
    // Idempotent: safe to call more than once.
    std::string_view finish() noexcept {
        write_header();
        sealed_ = true;
        finished_ = true;
        return std::string_view(buffer_, block_size_);
    }

    // Configured (and aligned) segment size in bytes.
    size_t block_size() const noexcept { return block_size_; }

    bool empty() const noexcept { return slot_count_ == 0; }

    // Greatest key stored in this block (the last successfully added key), or
    // an empty view if no entry was stored.  Returned as a zero-copy view into
    // this writer's buffer, so it is valid until reset()/destruction.
    std::string_view last_key() const noexcept {
        if (last_entry_offset_ < 0) { return std::string_view(); }
        const char *entry = buffer_ + last_entry_offset_;
        const uint32_t key_len = decode_fixed_32(entry);
        return std::string_view(entry + sizeof(uint32_t), key_len);
    }

    // Byte offset of the last stored entry within the segment, or -1 if empty.
    int64_t last_entry_offset() const noexcept { return last_entry_offset_; }

  private:
    // Encodes the header.  Must be called only when slot_count_ is final.
    void write_header() noexcept;

    size_t block_size_;
    char *buffer_;
    uint32_t slot_count_{0};
    size_t free_space_boundary_;
    int64_t last_entry_offset_{-1};
    bool sealed_{false};
    bool finished_{false};
};

} // namespace ANCHORDB_NAMESPACE
