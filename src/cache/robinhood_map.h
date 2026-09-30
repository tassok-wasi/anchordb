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
#include <cstddef>
#include <cstdint>
#include <vector>
namespace anchordb {

class RobinHoodMap : public Map {
  public:
    explicit RobinHoodMap(size_t initial_capacity = 16)
        : capacity_(round_upto_power_of_two_(initial_capacity)), size_(0) {
        buckets_.resize(capacity_);
    }

    const char *name() const override { return "anchordb.RobinHoodMap"; }

    // Uses pre-computed hash and returns true if found, populating out_value.
    bool find(const HashedKey &key_to_find, Slice &out_value) const override {
        if (size_ == 0)
            return false;

        size_t idx = mask_(key_to_find.hash);
        uint32_t current_dib = 0;

        while (buckets_[idx].occupied) {
            // Early termination: If current DIB in probing sequence is greater
            // than the bucket's DIB, the key cannot exist in the map.
            if (current_dib > buckets_[idx].dib) {
                return false;
            }

            if (buckets_[idx].key.hash == key_to_find.hash &&
                buckets_[idx].key.key == key_to_find.key) {
                out_value = buckets_[idx].value;
                return true;
            }

            idx = next_index_(idx);
            current_dib++;
        }

        return false;
    }

    // Inserts or updates an entry using its pre-computed hash.
    void insert(const HashedKey &key_to_insert, const Slice &value) override {
        if (size_ >= capacity_ * MAX_LOAD_FACTOR) {
            resize_(capacity_ * 2);
        }

        insert_(Bucket{key_to_insert, value, 0, true});
    }

    // Erases a key and shifts back elements to prevent tombstones.
    bool erase(const HashedKey &key_to_erase) override {
        if (size_ == 0)
            return false;

        size_t idx = mask_(key_to_erase.hash);
        uint32_t current_dib = 0;

        while (buckets_[idx].occupied) {
            if (current_dib > buckets_[idx].dib) {
                return false; // Key does not exist
            }

            if (buckets_[idx].key.hash == key_to_erase.hash &&
                buckets_[idx].key.key == key_to_erase.key) {
                // Key found -> Perform Backward Shift
                shift_back_(idx);
                size_--;
                return true;
            }

            idx = next_index_(idx);
            current_dib++;
        }

        return false;
    }

    void clear() override {
        size_ = 0;
        buckets_.clear();
    }

    // Capacity & Size Helpers
    size_t size() const override { return size_; }
    size_t capacity() const override { return capacity_; }

  private:
    struct Bucket {
        HashedKey key;
        Slice value;
        uint32_t dib{0};
        bool occupied{false};
    };

    static constexpr double MAX_LOAD_FACTOR = 0.85;
    std::vector<Bucket> buckets_;
    size_t capacity_;
    size_t size_;

    // Fast bitwise modulo (requires capacity_ to be a power of 2)
    size_t mask_(uint64_t hash) const {
        return static_cast<size_t>(hash & (capacity_ - 1));
    }

    size_t next_index_(size_t current_idx) const {
        return (current_idx + 1) & (capacity_ - 1);
    }

    static size_t round_upto_power_of_two_(size_t val) {
        return std::bit_ceil(val);
    }

    // Internal Robin Hood insertion core logic
    void insert_(Bucket incoming) {
        size_t idx = mask_(incoming.key.hash);

        while (true) {
            // Empty bucket found -> place entry directly
            if (!buckets_[idx].occupied) {
                buckets_[idx] = incoming;
                size_++;
                return;
            }

            // Key already exists -> overwrite value
            if (buckets_[idx].key.hash == incoming.key.hash &&
                buckets_[idx].key.key == incoming.key.key) {
                buckets_[idx].value = incoming.value;
                return;
            }

            // Robin Hood Eviction ("Steal from the rich, give to the
            // poor")
            if (incoming.dib > buckets_[idx].dib) {
                std::swap(incoming, buckets_[idx]);
            }

            // Advance probe step for displaced entry
            idx = next_index_(idx);
            incoming.dib++;
        }
    }

    // Backward-Shift Deletion algorithm
    void shift_back_(size_t delete_idx) {
        if (delete_idx >= buckets_.size()) {
            return;
        }

        size_t curr = delete_idx;
        size_t next = next_index_(curr);

        while (buckets_[next].occupied && buckets_[next].dib > 0) {
            buckets_[curr] = buckets_[next];
            buckets_[curr].dib--; // Entry is moved 1 step closer to home bucket
            curr = next;
            next = next_index_(next);
        }

        // Clear the trailing bucket
        buckets_[curr] = Bucket{};
    }

    // Resize and re-hash all entries
    void resize_(size_t new_capacity) {
        std::vector<Bucket> old_buckets = std::move(buckets_);

        capacity_ = new_capacity;
        size_ = 0;
        buckets_ = std::vector<Bucket>(capacity_);

        for (const auto &bucket : old_buckets) {
            if (bucket.occupied) {
                insert_(Bucket{bucket.key, bucket.value, 0, true});
            }
        }
    }
};

inline Map *robin_hood_map(size_t initial_capacity) {
    return new RobinHoodMap(initial_capacity);
}

} // namespace anchordb
