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

#include "anchordb/hash_map.h"
#include "anchordb/slice.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace anchordb {

class SwissMap : public Map {
  public:
    explicit SwissMap(size_t initial_capacity = 16)
        : capacity_(round_upto_power_of_two_(initial_capacity)), size_(0),
          num_deleted_(0) {
        // controls_ is over-allocated by one group width. The trailing bytes
        // mirror the first group and are kept in sync by set_control_(), which
        // lets a SIMD group load that crosses the end of the table observe the
        // same bytes the masked index arithmetic would.
        controls_.assign(capacity_ + kGroupWidth, CTRL_EMPTY);
        slots_.resize(capacity_);
    }

    const char *name() const override { return "anchordb.SwissMap"; }

    bool find(const HashedKey &key_to_find, Slice &out_value) const override;

    void insert(const HashedKey &key_to_insert, const Slice &value) override;

    bool erase(const HashedKey &key_to_erase) override;

    // Resets the map to empty while preserving the current capacity.
    void clear() override {
        size_ = 0;
        num_deleted_ = 0;
        controls_.assign(capacity_ + kGroupWidth, CTRL_EMPTY);
        slots_.assign(capacity_, Slot{});
    }

    size_t size() const override { return size_; }
    size_t capacity() const override { return capacity_; }

  private:
    struct Slot {
        HashedKey key;
        Slice value;
    };

    // Number of control bytes compared per SIMD probe step.
    static constexpr size_t kGroupWidth = 16;

    // Control byte status codes
    static constexpr int8_t CTRL_EMPTY = -128; // 0x80
    static constexpr int8_t CTRL_DELETED = -2; // 0xFE

    static constexpr double MAX_LOAD_FACTOR = 0.875;

    std::vector<int8_t> controls_;
    std::vector<Slot> slots_;
    size_t capacity_;
    size_t size_;
    size_t num_deleted_;

    static int8_t extract_h2_(uint64_t hash) {
        return static_cast<int8_t>((hash >> 57) & 0x7F);
    }

    size_t mask_(uint64_t hash) const {
        return static_cast<size_t>(hash & (capacity_ - 1));
    }

    static size_t round_upto_power_of_two_(size_t val) {
        size_t cap = 16;
        while (cap < val)
            cap <<= 1;
        return cap;
    }

    // Compares the group of kGroupWidth control bytes starting at `group`
    // against the 7-bit fingerprint `h2`.  Returns a bitmask whose set bits
    // index lanes equal to h2, and sets `has_empty` if any lane is CTRL_EMPTY.
    // Implemented with SIMD in swiss_map.cc.
    static uint32_t match_group_(const int8_t *group, int8_t h2,
                                 bool &has_empty);

    // Writes a control byte, mirroring it into the trailing padding group when
    // it falls in the first group so wrap-around SIMD loads stay consistent.
    void set_control_(size_t idx, int8_t value) {
        controls_[idx] = value;
        if (idx < kGroupWidth)
            controls_[capacity_ + idx] = value;
    }

    void resize_(size_t new_capacity);
};

} // namespace anchordb
