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

#include "anchordb/cache.h"
#include "anchordb/options.h"
#include "cache/linear_prob_map.h"
#include "cache/list.h"
#include "cache/robinhood_map.h"
#include "cache/swiss_map.h"
#include "cache/util.h"
#include "util/hash.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string_view>
#include <variant>
#include <vector>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

// ARC (Adaptive Replacement Cache).  Resident entries live in T1 (recent) or
// T2 (frequent); evicted entries become "ghosts" in B1/B2 whose hit pattern
// adapts the target size p_ of T1.
class ARCCache {
  public:
    explicit ARCCache(size_t capacity = 0, MapType t = MapType::Swiss)
        : capacity_(capacity), usage_(0), p_(0) {
        switch (t) {
        case MapType::LinearProbing:
            map_.emplace<LinearProbingMap<ARCHandle *>>(capacity_);
            break;
        case MapType::RobbinHood:
            map_.emplace<RobinHoodMap<ARCHandle *>>(capacity_);
            break;
        case MapType::Swiss: map_.emplace<SwissMap<ARCHandle *>>(capacity_);
        }
    }

    ~ARCCache() { prune(); }

    ARCCache(const ARCCache &) = delete;
    ARCCache &operator=(const ARCCache &) = delete;

    ARCCache(ARCCache &&other) noexcept : capacity_(other.capacity_) {
        other.mutex_.lock();
        usage_ = other.usage_;
        p_ = other.p_;
        t1_lru_ = std::move(other.t1_lru_);
        t1_in_use_ = std::move(other.t1_in_use_);
        t2_lru_ = std::move(other.t2_lru_);
        t2_in_use_ = std::move(other.t2_in_use_);
        b1_ = std::move(other.b1_);
        b2_ = std::move(other.b2_);
        map_ = std::move(other.map_);
        other.mutex_.unlock();
    }

    ARCCache &operator=(ARCCache &&other) noexcept {
        if (this != &other) {
            this->mutex_.lock();
            other.mutex_.lock();

            capacity_ = other.capacity_;
            usage_ = other.usage_;
            p_ = other.p_;
            t1_lru_ = std::move(other.t1_lru_);
            t1_in_use_ = std::move(other.t1_in_use_);
            t2_lru_ = std::move(other.t2_lru_);
            t2_in_use_ = std::move(other.t2_in_use_);
            b1_ = std::move(other.b1_);
            b2_ = std::move(other.b2_);
            map_ = std::move(other.map_);

            this->mutex_.unlock();
            other.mutex_.unlock();
        }
        return *this;
    }

    void set_capacity(size_t capacity) { capacity_ = capacity; }

    Cache::Handle *insert(std::string_view key, uint64_t hash, void *value,
                          size_t charge,
                          void (*deleter)(std::string_view key, void *value));
    Cache::Handle *lookup(std::string_view key, uint64_t hash);
    void release(Cache::Handle *handle);
    void erase(std::string_view key, uint64_t hash);
    void prune();
    size_t total_charge() const {
        mutex_.lock();
        size_t u = usage_;
        mutex_.unlock();
        return u;
    }

  private:
    bool finish_erase(ARCHandle *e);
    void ref(ARCHandle *e);
    void unref(ARCHandle *e);
    void replace(const HashedKey &key, bool in_b2);
    void remove_ghost_(ARCHandle *e);

    size_t capacity_;
    mutable std::mutex mutex_;
    size_t usage_;
    size_t p_; // Target size for T1

    // T1: Recent entries (split into unreferenced LRU and client-referenced
    // in_use)
    List<ARCHandle *> t1_lru_;
    List<ARCHandle *> t1_in_use_;

    // T2: Frequent entries (split into unreferenced LRU and client-referenced
    // in_use)
    List<ARCHandle *> t2_lru_;
    List<ARCHandle *> t2_in_use_;

    // B1 & B2: Ghost entries (metadata only, value evicted)
    List<ARCHandle *> b1_;
    List<ARCHandle *> b2_;

    std::variant<LinearProbingMap<ARCHandle *>, RobinHoodMap<ARCHandle *>,
                 SwissMap<ARCHandle *>>
        map_;
};

// Shards an ARC cache across kNumShards independent sub-caches, picking the
// shard from the top hash bits.
class ShardedARCCache : public Cache {
  public:
    explicit ShardedARCCache(size_t capacity, MapType t) : last_id_(0) {
        shard_.reserve(kNumShards);
        const size_t per_shard = (capacity + (kNumShards - 1)) / kNumShards;
        for (int s = 0; s < kNumShards; ++s) {
            shard_.emplace_back(per_shard, t);
        }
    }

    ~ShardedARCCache() override = default;

    Handle *insert(std::string_view key, void *value, size_t charge,
                   void (*deleter)(std::string_view key,
                                   void *value)) override {
        const uint64_t hash = hash_slice(key);
        return shard_[shard(hash)].insert(key, hash, value, charge, deleter);
    }

    Handle *lookup(std::string_view key) override {
        const uint64_t hash = hash_slice(key);
        return shard_[shard(hash)].lookup(key, hash);
    }

    void release(Handle *handle) override {
        ARCHandle *h = reinterpret_cast<ARCHandle *>(handle);
        shard_[shard(h->hash)].release(handle);
    }

    void erase(std::string_view key) override {
        const uint32_t hash = hash_slice(key);
        shard_[shard(hash)].erase(key, hash);
    }

    void *value(Handle *handle) override {
        return reinterpret_cast<ARCHandle *>(handle)->value;
    }

    uint64_t new_id() override {
        id_mutex_.lock();
        last_id_++;
        id_mutex_.unlock();
        return last_id_;
    }

    void prune() override {
        for (int s = 0; s < kNumShards; s++) { shard_[s].prune(); }
    }

    size_t total_charge() const override {
        size_t total = 0;
        for (int s = 0; s < kNumShards; s++) {
            total += shard_[s].total_charge();
        }
        return total;
    }

  private:
    std::vector<ARCCache> shard_;

    std::mutex id_mutex_;
    uint64_t last_id_;

    static inline uint64_t hash_slice(std::string_view &s) {
        return xx_hash64(s);
    }

    static uint64_t shard(uint64_t hash) {
        return hash >> (64 - kNumShardBits);
    }
};

} // namespace ANCHORDB_NAMESPACE
