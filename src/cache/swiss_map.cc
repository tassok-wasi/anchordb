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

#include "swiss_map.h"

// Provides the HAS_X86 / HAS_ARM porting macros and the SIMD intrinsic
// headers (x86intrin/immintrin or arm_neon) used by match_group_.
#include "port/port.h" // IWYU pragma: keep

namespace anchordb {

namespace {

// Portable count-trailing-zeros.  The input must be non-zero.
inline int count_trailing_zeros_(uint32_t x) {
#if defined(_MSC_VER)
    unsigned long index = 0;
    _BitScanForward(&index, x);
    return static_cast<int>(index);
#else
    return __builtin_ctz(x);
#endif
}

#if defined(HAS_ARM)
// Extracts a 16-bit lane mask from an ARM NEON 128-bit comparison vector,
// equivalent to x86's _mm_movemask_epi8.
uint32_t neon_mask_to_bitmask_(uint8x16_t mask_vec) {
    static const uint8_t bit_weights[16] = {1, 2, 4, 8, 16, 32, 64, 128,
                                            1, 2, 4, 8, 16, 32, 64, 128};
    const uint8x16_t weights = vld1q_u8(bit_weights);
    const uint8x16_t masked = vandq_u8(mask_vec, weights);

    // Pairwise-add the 16 masked bytes: the low 64-bit lane accumulates the
    // bit weights of the first 8 lanes, the high lane those of the last 8.
    const uint64x2_t sums = vpaddlq_u32(vpaddlq_u16(vpaddlq_u8(masked)));
    const uint64_t low = vgetq_lane_u64(sums, 0);
    const uint64_t high = vgetq_lane_u64(sums, 1);

    return static_cast<uint32_t>(low | (high << 8));
}
#endif

} // namespace

uint32_t SwissMap::match_group_(const int8_t *group, int8_t h2,
                                bool &has_empty) {
#if defined(HAS_X86)
    // x86 SSE2: load 16 control bytes and compare them in one shot.
    const __m128i group_vec =
        _mm_loadu_si128(reinterpret_cast<const __m128i *>(group));
    const uint32_t match_mask = static_cast<uint32_t>(
        _mm_movemask_epi8(_mm_cmpeq_epi8(group_vec, _mm_set1_epi8(h2))));
    has_empty = _mm_movemask_epi8(
                    _mm_cmpeq_epi8(group_vec, _mm_set1_epi8(CTRL_EMPTY))) != 0;
    return match_mask;

#elif defined(HAS_ARM)
    // ARM NEON: load 16 control bytes and compare them in one shot.
    const uint8x16_t group_vec =
        vld1q_u8(reinterpret_cast<const uint8_t *>(group));
    const uint32_t match_mask = neon_mask_to_bitmask_(
        vceqq_u8(group_vec, vdupq_n_u8(static_cast<uint8_t>(h2))));
    has_empty =
        neon_mask_to_bitmask_(vceqq_u8(
            group_vec, vdupq_n_u8(static_cast<uint8_t>(CTRL_EMPTY)))) != 0;
    return match_mask;

#else
    // Fallback scalar execution for unsupported hardware.  Because controls_
    // is padded with a mirrored group, reading kGroupWidth bytes directly is
    // always in-bounds and preserves probe order.
    uint32_t match_mask = 0;
    has_empty = false;
    for (size_t i = 0; i < kGroupWidth; ++i) {
        const int8_t ctrl = group[i];
        if (ctrl == h2)
            match_mask |= (1u << i);
        if (ctrl == CTRL_EMPTY)
            has_empty = true;
    }
    return match_mask;
#endif
}

bool SwissMap::find(const HashedKey &key_to_find, Slice &out_value) const {
    if (size_ == 0)
        return false;

    size_t idx = mask_(key_to_find.hash);
    const int8_t h2 = extract_h2_(key_to_find.hash); // 7-bit fingerprint

    while (true) {
        bool has_empty = false;
        uint32_t match_bitmask = match_group_(&controls_[idx], h2, has_empty);

        // Process all SIMD matches.
        while (match_bitmask != 0) {
            const int bit_idx = count_trailing_zeros_(match_bitmask);
            const size_t slot_idx =
                (idx + static_cast<size_t>(bit_idx)) & (capacity_ - 1);

            if (slots_[slot_idx].key.hash == key_to_find.hash &&
                slots_[slot_idx].key.key == key_to_find.key) {
                out_value = slots_[slot_idx].value;
                return true;
            }

            match_bitmask &= (match_bitmask - 1); // Clear lowest set bit
        }

        // Hit an empty bucket -> key cannot exist.
        if (has_empty)
            return false;

        // Advance probe sequence by one group.
        idx = (idx + kGroupWidth) & (capacity_ - 1);
    }
}

void SwissMap::insert(const HashedKey &key_to_insert, const Slice &value) {
    // Grow or purge tombstones once the combined (live + deleted) load would
    // exceed the maximum load factor.  Keeping this below 1 guarantees at
    // least one CTRL_EMPTY slot always exists, so probe sequences terminate.
    if (size_ + num_deleted_ + 1 >
        static_cast<size_t>(capacity_ * MAX_LOAD_FACTOR)) {
        if (size_ * 2 <= capacity_) {
            resize_(capacity_); // Rehash in place: drop tombstones.
        } else {
            resize_(capacity_ * 2); // Grow.
        }
    }

    const int8_t h2 = extract_h2_(key_to_insert.hash);
    size_t idx = mask_(key_to_insert.hash);

    // First usable slot found along the probe sequence.  Insertion must not
    // stop at the first DELETED slot: an existing copy of the key may live
    // further along the sequence, past that tombstone.
    size_t insert_idx = capacity_;
    bool insert_idx_is_deleted = false;

    while (true) {
        bool has_empty = false;
        uint32_t match_bitmask = match_group_(&controls_[idx], h2, has_empty);

        // Look for an existing entry to overwrite.
        while (match_bitmask != 0) {
            const int bit_idx = count_trailing_zeros_(match_bitmask);
            const size_t slot_idx =
                (idx + static_cast<size_t>(bit_idx)) & (capacity_ - 1);

            if (slots_[slot_idx].key.hash == key_to_insert.hash &&
                slots_[slot_idx].key.key == key_to_insert.key) {
                slots_[slot_idx].value = value;
                return;
            }

            match_bitmask &= (match_bitmask - 1);
        }

        // Remember the first reusable slot encountered in probe order.
        if (insert_idx == capacity_) {
            for (size_t i = 0; i < kGroupWidth; ++i) {
                const size_t slot_idx = (idx + i) & (capacity_ - 1);
                const int8_t ctrl = controls_[slot_idx];
                if (ctrl == CTRL_EMPTY || ctrl == CTRL_DELETED) {
                    insert_idx = slot_idx;
                    insert_idx_is_deleted = (ctrl == CTRL_DELETED);
                    break;
                }
            }
        }

        // Reached the end of the probe sequence without finding the key.
        if (has_empty)
            break;

        idx = (idx + kGroupWidth) & (capacity_ - 1);
    }

    if (insert_idx_is_deleted)
        --num_deleted_;

    set_control_(insert_idx, h2);
    slots_[insert_idx] = Slot{key_to_insert, value};
    ++size_;
}

bool SwissMap::erase(const HashedKey &key_to_erase) {
    if (size_ == 0)
        return false;

    size_t idx = mask_(key_to_erase.hash);
    const int8_t h2 = extract_h2_(key_to_erase.hash);

    while (true) {
        bool has_empty = false;
        uint32_t match_bitmask = match_group_(&controls_[idx], h2, has_empty);

        while (match_bitmask != 0) {
            const int bit_idx = count_trailing_zeros_(match_bitmask);
            const size_t slot_idx =
                (idx + static_cast<size_t>(bit_idx)) & (capacity_ - 1);

            if (slots_[slot_idx].key.hash == key_to_erase.hash &&
                slots_[slot_idx].key.key == key_to_erase.key) {
                set_control_(slot_idx, CTRL_DELETED);
                slots_[slot_idx] = Slot{};
                --size_;
                ++num_deleted_;
                return true;
            }

            match_bitmask &= (match_bitmask - 1);
        }

        if (has_empty)
            return false;

        idx = (idx + kGroupWidth) & (capacity_ - 1);
    }
}

void SwissMap::resize_(size_t new_capacity) {
    std::vector<int8_t> old_ctrls = std::move(controls_);
    std::vector<Slot> old_slots = std::move(slots_);

    capacity_ = new_capacity;
    size_ = 0;
    num_deleted_ = 0;
    controls_.assign(capacity_ + kGroupWidth, CTRL_EMPTY);
    slots_.assign(capacity_, Slot{});

    for (size_t i = 0; i < old_slots.size(); ++i) {
        if (old_ctrls[i] != CTRL_EMPTY && old_ctrls[i] != CTRL_DELETED) {
            insert(old_slots[i].key, old_slots[i].value);
        }
    }
}

Map *swiss_map(size_t initial_capacity) {
    return new SwissMap(initial_capacity);
}

} // namespace anchordb
