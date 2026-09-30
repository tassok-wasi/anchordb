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
#include <bit>
#include <cstdint>
#include <utility>
#include <vector>

namespace anchordb {

class LinearProbingMap : public Map {
  public:
    explicit LinearProbingMap(size_t initial_capacity = 16)
        : capacity_(round_upto_power_of_two_(initial_capacity)), size_(0) {
        buckets_.resize(capacity_);
    }

    const char *name() const override { return "anchordb.LinearProbingMap"; }

    bool find(const HashedKey &key_to_find, Slice &out_value) const override {
        if (size_ == 0)
            return false;

        size_t idx = mask_(key_to_find.hash);

        while (buckets_[idx].occupied) {
            if (buckets_[idx].key.hash == key_to_find.hash &&
                buckets_[idx].key.key == key_to_find.key) {
                out_value = buckets_[idx].value;
                return true;
            }
            idx = next_index_(idx);
        }

        return false;
    }

    void insert(const HashedKey &key_to_insert, const Slice &value) override {
        if (size_ >= capacity_ * MAX_LOAD_FACTOR) {
            resize_(capacity_ * 2);
        }

        size_t idx = mask_(key_to_insert.hash);

        while (buckets_[idx].occupied) {
            // Overwrite existing key
            if (buckets_[idx].key.hash == key_to_insert.hash &&
                buckets_[idx].key.key == key_to_insert.key) {
                buckets_[idx].value = value;
                return;
            }
            idx = next_index_(idx);
        }

        // Claim empty bucket
        buckets_[idx] = Bucket{key_to_insert, value, true};
        size_++;
    }

    bool erase(const HashedKey &key_to_erase) override {
        if (size_ == 0)
            return false;

        size_t idx = mask_(key_to_erase.hash);

        while (buckets_[idx].occupied) {
            if (buckets_[idx].key.hash == key_to_erase.hash &&
                buckets_[idx].key.key == key_to_erase.key) {
                shift_back(idx);
                size_--;
                return true;
            }
            idx = next_index_(idx);
        }

        return false;
    }

    void clear() override {
        size_ = 0;
        buckets_.clear();
    }

    size_t size() const override { return size_; }
    size_t capacity() const override { return capacity_; }

  private:
    struct Bucket {
        HashedKey key;
        Slice value;
        bool occupied{false};
    };

    // Keep max load factor lower for standard linear probing to prevent
    // clustering
    static constexpr double MAX_LOAD_FACTOR = 0.65;

    std::vector<Bucket> buckets_;
    size_t capacity_;
    size_t size_;

    size_t mask_(uint64_t hash) const {
        return static_cast<size_t>(hash & (capacity_ - 1));
    }

    size_t next_index_(size_t current_idx) const {
        return (current_idx + 1) & (capacity_ - 1);
    }

    static size_t round_upto_power_of_two_(size_t val) {
        return std::bit_ceil(val);
    }

    // Shifts back elements that collided behind the deleted slot
    void shift_back(size_t delete_idx) {
        size_t curr = delete_idx;
        size_t next = next_index_(curr);

        while (buckets_[next].occupied) {
            size_t home_bucket = mask_(buckets_[next].key.hash);

            // Check if the entry in 'next' can be moved back to 'curr'
            if ((curr < next && (home_bucket <= curr || home_bucket > next)) ||
                (curr > next && (home_bucket <= curr && home_bucket > next))) {
                buckets_[curr] = buckets_[next];
                curr = next;
            }
            next = next_index_(next);
        }

        buckets_[curr] = Bucket{};
    }

    void resize_(size_t new_capacity) {
        std::vector<Bucket> old_buckets = std::move(buckets_);
        capacity_ = new_capacity;
        size_ = 0;
        buckets_ = std::vector<Bucket>(capacity_);

        for (const auto &bucket : old_buckets) {
            if (bucket.occupied) {
                size_t idx = mask_(bucket.key.hash);
                while (buckets_[idx].occupied) {
                    idx = next_index_(idx);
                }
                buckets_[idx] = bucket;
                size_++;
            }
        }
    }
};

inline Map *linear_probing_map(size_t initial_capacity) {
    return new LinearProbingMap(initial_capacity);
}

} // namespace anchordb
