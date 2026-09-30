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
#include "port/port.h"
#include <cstdint>
#include <cstring>
#include <vector>

namespace anchordb {

class SwissMap : public Map {
  public:
    explicit SwissMap(size_t initial_capacity = 16)
        : capacity_(round_upto_power_of_two_(initial_capacity)), size_(0) {
        controls_.resize(capacity_, CTRL_EMPTY);
        slots_.resize(capacity_);
    }

    const char *name() const override { return "anchordb.SwissMap"; }

    bool find(const HashedKey &key_to_find, Slice &out_value) const override;

    void insert(const HashedKey &key_to_insert, const Slice &value) override;

    bool erase(const HashedKey &key_to_erase) override;

    void clear() override {
        size_ = 0;
        controls_.clear();
        slots_.clear();
    }

    size_t size() const override { return size_; }
    size_t capacity() const override { return capacity_; }

  private:
    struct Slot {
        HashedKey key;
        Slice value;
    };

    // Control byte status codes
    static constexpr int8_t CTRL_EMPTY = -128; // 0x80
    static constexpr int8_t CTRL_DELETED = -2; // 0xFE

    static constexpr double MAX_LOAD_FACTOR = 0.875;

    std::vector<int8_t> controls_;
    std::vector<Slot> slots_;
    size_t capacity_;
    size_t size_;

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

    void resize_(size_t new_capacity);
};

} // namespace anchordb
