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

#include "sstable/idx_block_writer.h"
#include "util/coding.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

// Index block layout
// clang-format off
/*
    +--------------------------------------------------------------------------------------------------------+
    | 64KB ALIGNED SEGMENT BOUNDARY                                                                          |
    +--------------------------------------------------------------------------------------------------------+
    | [HEADER (4 Bytes)]                                                                                     |
    |   - slot_count          (2 Bytes): Total elements in this block                                        |
    |   - free_space_boundary (2 Bytes): Byte offset marking the boundary where backward payload begins      |
    +--------------------------------------------------------------------------------------------------------+
    | [FORWARD FIXED-SIZE SLOTS ARRAY (Grows forward from byte offset 4)]                                    |
    |   - Slot 0: [key_local_offset (2B) | value_type (1B) | value_flag (1B) | data_size (4B) | data_global_offset (8B)] |
    |   - Slot 1: [key_local_offset (2B) | value_type (1B) | value_flag (1B) | data_size (4B) | data_global_offset (8B)] |
    |   - Slot N: Totaling 16 bytes per slot. Fits exactly 4 slots per 64-byte CPU cache line.              |
    |                                                                                                        |
    |       Field Metadata Definitions:                                                                      |
    |         - value_type (1B): [0x1 = Value] | [0x0 = Tombstone]                                           |
    |         - value_flag (1B): [0x1 = Inline in data.bin] | [0x0 = Pointer to vLog]                        |
    |         - data_size  (4B): Total encoded layout size of the referenced data record in data.bin         |
    +--------------------------------------------------------------------------------------------------------+
    |                                  ... UNUSED BLOCK SPACE ...                                            |
    +--------------------------------------------------------------------------------------------------------+
    | [BACKWARD VARIABLE-LENGTH STRINGS PAYLOAD (Grows backward from absolute end of 64KB block)]            |
    |   - String N: [key_length (2B)] [key_bytes (Variable)] [timestamp (8B: uint64_t)]                      |
    |   - String 0: Read sequentially using 2-byte key_local_offset pointers from forward slots.             |
    +--------------------------------------------------------------------------------------------------------+
*/
// clang-format on

void IndexBlockWriter::write_header() noexcept {
    encode_fixed_16(buffer_, slot_count_);
    // 0 is the empty-block sentinel: an empty block's boundary is 65536, which
    // does not fit in 16 bits, and no non-empty block can ever have a boundary
    // of 0 (the forward slot array occupies the low bytes).
    const uint16_t boundary =
        (slot_count_ == 0) ? 0 : static_cast<uint16_t>(free_space_boundary_);
    encode_fixed_16(buffer_ + sizeof(uint16_t), boundary);
}

bool IndexBlockWriter::add(std::string_view key, ValueHandle handle,
                           uint64_t timestamp) {
    assert(!finished_);

    // A key longer than 16 bits cannot be encoded by this format; an oversized
    // entry can never fit in a segment and must be handled by the caller.
    const size_t payload_size =
        sizeof(uint16_t) + key.size() + sizeof(uint64_t);
    if (finished_ || sealed_ || key.size() > UINT16_MAX ||
        payload_size > static_cast<size_t>(kDefaultBlockSize)) {
        return false;
    }

    const size_t forward_end =
        kIndexHeaderSize + static_cast<size_t>(slot_count_) * kIndexSlotSize;
    const size_t required = kIndexSlotSize + payload_size;
    const size_t available =
        static_cast<size_t>(free_space_boundary_) - forward_end;

    // Not enough room for this entry: seal the block and let the caller start a
    // new one.  The rejected entry is not stored.
    if (required > available) {
        write_header();
        sealed_ = true;
        return false;
    }

    // Payload grows down from the free-space boundary.
    const int new_boundary =
        free_space_boundary_ - static_cast<int>(payload_size);
    char *payload = buffer_ + new_boundary;
    encode_fixed_16(payload, static_cast<uint16_t>(key.size()));
    payload += sizeof(uint16_t);
    std::memcpy(payload, key.data(), key.size());
    payload += key.size();
    encode_fixed_64(payload, timestamp);
    free_space_boundary_ = new_boundary;
    last_entry_offset_ = new_boundary;

    // Fixed-size slot grows up from the header.
    char *slot = buffer_ + forward_end;
    encode_fixed_16(slot + kSlotKeyOffsetOff,
                    static_cast<uint16_t>(new_boundary)); // key offset
    // Byte order matches the documented layout: value_type, value_flag,
    // data_size, then data_global_offset.
    slot[kSlotTypeOff] = static_cast<char>(static_cast<uint8_t>(handle.type));
    slot[kSlotFlagOff] = static_cast<char>(static_cast<uint8_t>(handle.flag));
    encode_fixed_32(slot + kSlotDataSizeOff, handle.data_size);
    encode_fixed_64(slot + kSlotGlobalOffsetOff, handle.offset);

    ++slot_count_;
    return true;
}

} // namespace ANCHORDB_NAMESPACE
