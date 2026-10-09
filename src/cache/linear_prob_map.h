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
#include <utility>
#include <vector>

namespace anchordb {

template <typename T> class LinearProbingMap {
  public:
    explicit LinearProbingMap(size_t initial_capacity = 16)
        : capacity_(round_upto_power_of_two_(initial_capacity)), size_(0) {
        buckets_.resize(capacity_);
    }

    const char *name() const { return "anchordb.LinearProbingMap"; }

    bool contains(const HashedKey &key) const;
    T get(const HashedKey &key) const;
    void insert(const HashedKey &key, T value);
    T remove(const HashedKey &key);

    void clear() {
        size_ = 0;
        buckets_.clear();
    }

    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }

  private:
    struct Bucket {
        HashedKey hashed_key;
        T value;
        bool occupied{false};
    };

    // Keep max load factor lower for standard linear probing to prevent
    // clustering
    static constexpr double kMaxLoadFactor = 0.65;

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
    void shift_back_(size_t delete_idx);

    void resize_(size_t new_capacity);
};

template <typename T>
bool LinearProbingMap<T>::contains(const HashedKey &key) const {
    if (size_ == 0)
        return false;

    size_t idx = mask_(key.hash);

    while (buckets_[idx].occupied) {
        if (buckets_[idx].hashed_key.hash == key.hash &&
            buckets_[idx].hashed_key.key == key.key) {
            return true;
        }
        idx = next_index_(idx);
    }

    return false;
}

template <typename T> T LinearProbingMap<T>::get(const HashedKey &key) const {
    if (size_ == 0) {
        return T();
    }

    size_t idx = mask_(key.hash);

    while (buckets_[idx].occupied) {
        if (buckets_[idx].hashed_key.hash == key.hash &&
            buckets_[idx].hashed_key.key == key.key) {
            return buckets_[idx].value;
        }
        idx = next_index_(idx);
    }

    return T();
}

template <typename T>
void LinearProbingMap<T>::insert(const HashedKey &key, T value) {
    if (size_ >= capacity_ * kMaxLoadFactor) {
        resize_(capacity_ * 2);
    }

    size_t idx = mask_(key.hash);

    while (buckets_[idx].occupied) {
        // Overwrite existing key
        if (buckets_[idx].hashed_key.hash == key.hash &&
            buckets_[idx].hashed_key.key == key.key) {
            buckets_[idx].value = value;
            return;
        }
        idx = next_index_(idx);
    }

    // Claim empty bucket
    buckets_[idx] = Bucket{key, value, true};
    size_++;
}

template <typename T> T LinearProbingMap<T>::remove(const HashedKey &key) {
    if (size_ == 0)
        return T();

    size_t idx = mask_(key.hash);

    T result;

    while (buckets_[idx].occupied) {
        if (buckets_[idx].hashed_key.hash == key.hash &&
            buckets_[idx].hashed_key.key == key.key) {
            result = buckets_[idx].value;
            shift_back_(idx);
            size_--;
            return result;
        }
        idx = next_index_(idx);
    }

    return T();
}

template <typename T> void LinearProbingMap<T>::shift_back_(size_t delete_idx) {
    size_t curr = delete_idx;
    size_t next = next_index_(curr);

    while (buckets_[next].occupied) {
        size_t home_bucket = mask_(buckets_[next].hashed_key.hash);

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

template <typename T> void LinearProbingMap<T>::resize_(size_t new_capacity) {
    std::vector<Bucket> old_buckets = std::move(buckets_);
    capacity_ = new_capacity;
    size_ = 0;
    buckets_ = std::vector<Bucket>(capacity_);

    for (const auto &bucket : old_buckets) {
        if (bucket.occupied) {
            size_t idx = mask_(bucket.hashed_key.hash);
            while (buckets_[idx].occupied) {
                idx = next_index_(idx);
            }
            buckets_[idx] = bucket;
            size_++;
        }
    }
}

} // namespace anchordb
