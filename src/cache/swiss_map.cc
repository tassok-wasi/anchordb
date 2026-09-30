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

namespace anchordb {

bool SwissMap::find(const HashedKey &key_to_find, Slice &out_value) const {
    if (size_ == 0)
        return false;

    size_t idx = mask_(key_to_find.hash);
    int8_t h2 = extract_h2_(key_to_find.hash); // 7-bit fingerprint

    while (true) {
        uint32_t match_bitmask = 0;
        bool has_empty = false;

        // --- SIMD Branching based on Target Architecture ---
#if defined(HAS_X86)
        // x86 SSE2: Load 16 control bytes & compare in 1 CPU cycle
        __m128i target = _mm_set1_epi8(h2);
        __m128i group =
            _mm_loadu_si128(reinterpret_cast<const __m128i *>(&controls_[idx]));

        __m128i match_mask = _mm_cmpeq_epi8(group, target);
        match_bitmask = static_cast<uint32_t>(_mm_movemask_epi8(match_mask));

        __m128i empty_mask = _mm_cmpeq_epi8(group, _mm_set1_epi8(CTRL_EMPTY));
        has_empty = (_mm_movemask_epi8(empty_mask) != 0);

#elif defined(HAS_ARM)
        // ARM NEON: Load 16 control bytes & compare in 1 CPU cycle
        uint8x16_t target = vdupq_n_u8(static_cast<uint8_t>(h2));
        uint8x16_t group =
            vld1q_u8(reinterpret_cast<const uint8_t *>(&controls_[idx]));

        uint8x16_t match_vec = vceqq_u8(group, target);
        match_bitmask = neon_mask_to_bitmask_(match_vec);

        uint8x16_t empty_vec =
            vceqq_u8(group, vdupq_n_u8(static_cast<uint8_t>(CTRL_EMPTY)));
        has_empty = (neon_mask_to_bitmask_(empty_vec) != 0);

#else
        // Fallback Scalar execution for unsupported hardware
        for (size_t i = 0; i < 16; ++i) {
            size_t probe_idx = (idx + i) & (capacity_ - 1);
            if (controls_[probe_idx] == h2) {
                match_bitmask |= (1u << i);
            }
            if (controls_[probe_idx] == CTRL_EMPTY) {
                has_empty = true;
            }
        }
#endif

        // Process all SIMD matches
        while (match_bitmask != 0) {
            int bit_idx = __builtin_ctz(match_bitmask); // Count trailing zeros
            size_t slot_idx = (idx + bit_idx) & (capacity_ - 1);

            if (slots_[slot_idx].key.hash == key_to_find.hash &&
                slots_[slot_idx].key.key == key_to_find.key) {
                out_value = slots_[slot_idx].value;
                return true;
            }

            match_bitmask &= (match_bitmask - 1); // Clear lowest set bit
        }

        // Hit an empty bucket -> key cannot exist
        if (has_empty) {
            return false;
        }

        // Advance probe sequence by 16 group size
        idx = (idx + 16) & (capacity_ - 1);
    }
}

void SwissMap::insert(const HashedKey &key_to_insert, const Slice &value) {
    if (size_ >= capacity_ * MAX_LOAD_FACTOR) {
        resize_(capacity_ * 2);
    }

    size_t idx = mask_(key_to_insert.hash);
    int8_t h2 = extract_h2_(key_to_insert.hash);

    while (true) {
        for (size_t i = 0; i < 16; ++i) {
            size_t slot_idx = (idx + i) & (capacity_ - 1);
            int8_t ctrl = controls_[slot_idx];

            // Key already exists -> Overwrite value
            if (ctrl == h2 && slots_[slot_idx].key.hash == key_to_insert.hash &&
                slots_[slot_idx].key.key == key_to_insert.key) {
                slots_[slot_idx].value = value;
                return;
            }

            // First available bucket found
            if (ctrl == CTRL_EMPTY || ctrl == CTRL_DELETED) {
                controls_[slot_idx] = h2;
                slots_[slot_idx] = Slot{key_to_insert, value};
                size_++;
                return;
            }
        }

        idx = (idx + 16) & (capacity_ - 1);
    }
}

bool SwissMap::erase(const HashedKey &key_to_erase) {
    if (size_ == 0)
        return false;

    size_t idx = mask_(key_to_erase.hash);
    int8_t h2 = extract_h2_(key_to_erase.hash);

    while (true) {
        uint32_t match_bitmask = 0;
        bool has_empty = false;

#if defined(HAS_X86)
        __m128i target = _mm_set1_epi8(h2);
        __m128i group =
            _mm_loadu_si128(reinterpret_cast<const __m128i *>(&controls_[idx]));

        match_bitmask = static_cast<uint32_t>(
            _mm_movemask_epi8(_mm_cmpeq_epi8(group, target)));
        has_empty = (_mm_movemask_epi8(_mm_cmpeq_epi8(
                         group, _mm_set1_epi8(CTRL_EMPTY))) != 0);

#elif defined(HAS_ARM)
        uint8x16_t target = vdupq_n_u8(static_cast<uint8_t>(h2));
        uint8x16_t group =
            vld1q_u8(reinterpret_cast<const uint8_t *>(&controls_[idx]));

        match_bitmask = neon_mask_to_bitmask_(vceqq_u8(group, target));
        has_empty =
            (neon_mask_to_bitmask_(vceqq_u8(
                 group, vdupq_n_u8(static_cast<uint8_t>(CTRL_EMPTY)))) != 0);

#else
        for (size_t i = 0; i < 16; ++i) {
            size_t probe_idx = (idx + i) & (capacity_ - 1);
            if (controls_[probe_idx] == h2)
                match_bitmask |= (1u << i);
            if (controls_[probe_idx] == CTRL_EMPTY)
                has_empty = true;
        }
#endif

        while (match_bitmask != 0) {
            int bit_idx = __builtin_ctz(match_bitmask);
            size_t slot_idx = (idx + bit_idx) & (capacity_ - 1);

            if (slots_[slot_idx].key.hash == key_to_erase.hash &&
                slots_[slot_idx].key.key == key_to_erase.key) {
                controls_[slot_idx] = CTRL_DELETED;
                slots_[slot_idx] = Slot{};
                size_--;
                return true;
            }

            match_bitmask &= (match_bitmask - 1);
        }

        if (has_empty) {
            return false;
        }

        idx = (idx + 16) & (capacity_ - 1);
    }
}

void SwissMap::resize_(size_t new_capacity) {
    std::vector<int8_t> old_ctrls = std::move(controls_);
    std::vector<Slot> old_slots = std::move(slots_);

    capacity_ = new_capacity;
    size_ = 0;
    controls_ = std::vector<int8_t>(capacity_, CTRL_EMPTY);
    slots_ = std::vector<Slot>(capacity_);

    for (size_t i = 0; i < old_ctrls.size(); ++i) {
        if (old_ctrls[i] != CTRL_EMPTY && old_ctrls[i] != CTRL_DELETED) {
            insert(old_slots[i].key, old_slots[i].value);
        }
    }
}

#if defined(HAS_ARM)
namespace {
// Helper to extract a 16-bit mask from an ARM NEON 128-bit comparison
// vector
uint32_t SwissMap::neon_mask_to_bitmask_(uint8x16_t mask_vec) {
    static const uint8_t shift_bytes[16] = {1, 2, 4, 8, 16, 32, 64, 128,
                                            1, 2, 4, 8, 16, 32, 64, 128};
    uint8x16_t shift = vld1q_u8(shift_bytes);
    uint8x16_t masked = vandq_u8(mask_vec, shift);

    // Sum high and low 64-bit halves
    uint64x2_t pair = vpaddlq_u32(vpaddlq_u16(vpaddlq_u8(masked)));
    uint64_t low = vgetq_lane_u64(pair, 0);
    uint64_t high = vgetq_lane_u64(pair, 1);

    return static_cast<uint32_t>(low | (high << 8));
}
} // namespace
#endif

Map *swiss_map(size_t initial_capacity) {
    return new SwissMap(initial_capacity);
}

} // namespace anchordb
