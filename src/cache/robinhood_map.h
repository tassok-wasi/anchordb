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

#include "util.h"
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>
#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

template <typename T> class RobinHoodMap {
  public:
    explicit RobinHoodMap(size_t initial_capacity = 16)
        : capacity_(round_upto_power_of_two_(initial_capacity)), size_(0) {
        buckets_.resize(capacity_);
    }

    const char *name() const { return "anchordb.RobinHoodMap"; }
    bool contains(HashedKey key) const;
    T get(HashedKey key) const;

    // Inserts or updates an entry using its pre-computed hash.
    void insert(HashedKey key, T value) {
        if (size_ >= capacity_ * kMaxLoadFactor) {
            resize_(capacity_ * 2);
        }

        insert_(Bucket{key, value, 0, true});
    }

    // Erases a key and shifts back elements to prevent tombstones.
    T remove(HashedKey key);

    void clear() {
        size_ = 0;
        buckets_.clear();
    }

    // Capacity & Size Helpers
    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }

  private:
    struct Bucket {
        HashedKey hashed_key;
        T value;
        uint32_t dib{0};
        bool occupied{false};
    };

    static constexpr double kMaxLoadFactor = 0.85;
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
    void insert_(Bucket incoming);

    // Backward-Shift Deletion algorithm
    void shift_back_(size_t delete_idx);

    // Resize and re-hash all entries
    void resize_(size_t new_capacity);
};

template <typename T> bool RobinHoodMap<T>::contains(HashedKey key) const {
    if (size_ == 0)
        return false;

    size_t idx = mask_(key.hash);
    uint32_t current_dib = 0;

    while (buckets_[idx].occupied) {
        // Early termination: If current DIB in probing sequence is greater
        // than the bucket's DIB, the key cannot exist in the map.
        if (current_dib > buckets_[idx].dib) {
            return false;
        }

        if (buckets_[idx].hashed_key.hash == key.hash &&
            buckets_[idx].hashed_key.key == key.key) {
            return true;
        }

        idx = next_index_(idx);
        current_dib++;
    }

    return false;
}

template <typename T> T RobinHoodMap<T>::get(HashedKey key) const {
    if (size_ == 0)
        return T();

    size_t idx = mask_(key.hash);
    uint32_t current_dib = 0;

    while (buckets_[idx].occupied) {
        // Early termination: If current DIB in probing sequence is greater
        // than the bucket's DIB, the key cannot exist in the map.
        if (current_dib > buckets_[idx].dib) {
            return T();
        }

        if (buckets_[idx].hashed_key.hash == key.hash &&
            buckets_[idx].hashed_key.key == key.key) {
            return buckets_[idx].value;
        }

        idx = next_index_(idx);
        current_dib++;
    }

    return T();
}

template <typename T> void RobinHoodMap<T>::insert_(Bucket incoming) {
    size_t idx = mask_(incoming.hashed_key.hash);

    while (true) {
        // Empty bucket found -> place entry directly
        if (!buckets_[idx].occupied) {
            buckets_[idx] = incoming;
            size_++;
            return;
        }

        // Key already exists -> overwrite value
        if (buckets_[idx].hashed_key.hash == incoming.hashed_key.hash &&
            buckets_[idx].hashed_key.key == incoming.hashed_key.key) {
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

template <typename T> T RobinHoodMap<T>::remove(HashedKey key) {
    if (size_ == 0)
        return T();

    size_t idx = mask_(key.hash);
    uint32_t current_dib = 0;

    T result;

    while (buckets_[idx].occupied) {
        if (current_dib > buckets_[idx].dib) {
            return T(); // Key does not exist
        }

        if (buckets_[idx].hashed_key.hash == key.hash &&
            buckets_[idx].hashed_key.key == key.key) {
            result = buckets_[idx].value;
            shift_back_(idx);
            size_--;
            return result;
        }

        idx = next_index_(idx);
        current_dib++;
    }

    return T();
}

template <typename T> void RobinHoodMap<T>::shift_back_(size_t delete_idx) {
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

template <typename T> void RobinHoodMap<T>::resize_(size_t new_capacity) {
    std::vector<Bucket> old_buckets = std::move(buckets_);

    capacity_ = new_capacity;
    size_ = 0;
    buckets_ = std::vector<Bucket>(capacity_);

    for (const auto &bucket : old_buckets) {
        if (bucket.occupied) {
            insert_(Bucket{bucket.hashed_key, bucket.value, 0, true});
        }
    }
}

} // namespace anchordb
