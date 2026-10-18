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
#include "cache/circular_ring.h"
#include "cache/linear_prob_map.h"
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

class ClockProCache {
  public:
    explicit ClockProCache(size_t capacity = 0, MapType t = MapType::Swiss)
        : capacity_(capacity), usage_(0), hot_usage_(0) {

        // Target cold capacity defaults to 100% capacity initially, dynamically
        // adjusts
        cold_capacity_ = capacity_;

        switch (t) {
        case MapType::LinearProbing:
            map_.emplace<LinearProbingMap<ClockProHandle *>>(capacity_);
            break;
        case MapType::RobbinHood:
            map_.emplace<RobinHoodMap<ClockProHandle *>>(capacity_);
            break;
        case MapType::Swiss:
            map_.emplace<SwissMap<ClockProHandle *>>(capacity_);
        }
    }
    // Frees every entry still held by the cache. Callers must have released
    // all outstanding handles before destroying the cache.
    ~ClockProCache() { prune(); }

    ClockProCache(const ClockProCache &) = delete;
    ClockProCache &operator=(const ClockProCache &) = delete;

    ClockProCache(ClockProCache &&other) noexcept : capacity_(other.capacity_) {
        other.mutex_.lock();
        usage_ = other.usage_;
        hot_usage_ = other.hot_usage_;
        cold_capacity_ = other.cold_capacity_;
        ring_ = std::move(other.ring_);
        hand_cold_ = other.hand_cold_;
        hand_hot_ = other.hand_hot_;
        hand_test_ = other.hand_test_;
        map_ = std::move(other.map_);

        other.hand_cold_ = nullptr;
        other.hand_hot_ = nullptr;
        other.hand_test_ = nullptr;
        other.mutex_.unlock();
    }

    ClockProCache &operator=(ClockProCache &&other) noexcept {
        if (this != &other) {
            this->mutex_.lock();
            other.mutex_.lock();

            capacity_ = other.capacity_;
            usage_ = other.usage_;
            hot_usage_ = other.hot_usage_;
            cold_capacity_ = other.cold_capacity_;
            ring_ = std::move(other.ring_);
            hand_cold_ = other.hand_cold_;
            hand_hot_ = other.hand_hot_;
            hand_test_ = other.hand_test_;
            map_ = std::move(other.map_);

            other.hand_cold_ = nullptr;
            other.hand_hot_ = nullptr;
            other.hand_test_ = nullptr;

            this->mutex_.unlock();
            other.mutex_.unlock();
        }
        return *this;
    }

    void set_capacity(size_t capacity) {
        capacity_ = capacity;
        cold_capacity_ = capacity_;
    }

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
    void hand_cold_step();
    void hand_hot_step();
    void hand_test_step();
    void evict();

    void ring_add(ClockProHandle *e);
    void ring_remove(ClockProHandle *e);

    void ref(ClockProHandle *e);
    void unref(ClockProHandle *e);
    bool finish_erase(ClockProHandle *e);

    size_t capacity_;
    size_t cold_capacity_;
    mutable std::mutex mutex_;
    size_t usage_;
    size_t hot_usage_;

    CircularRing ring_;
    ClockProHandle *hand_cold_ = nullptr;
    ClockProHandle *hand_hot_ = nullptr;
    ClockProHandle *hand_test_ = nullptr;

    std::variant<LinearProbingMap<ClockProHandle *>,
                 RobinHoodMap<ClockProHandle *>, SwissMap<ClockProHandle *>>
        map_;
};

class ShardedClockProCache : public Cache {
  public:
    explicit ShardedClockProCache(size_t capacity, MapType t) : last_id_(0) {
        shard_.reserve(kNumShards);
        const size_t per_shard = (capacity + (kNumShards - 1)) / kNumShards;
        for (int s = 0; s < kNumShards; ++s) {
            shard_.emplace_back(per_shard, t);
        }
    }

    ~ShardedClockProCache() override {};

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
        ClockProHandle *h = reinterpret_cast<ClockProHandle *>(handle);
        shard_[shard(h->hash)].release(handle);
    }

    void erase(std::string_view key) override {
        const uint64_t hash = hash_slice(key);
        shard_[shard(hash)].erase(key, hash);
    }

    void *value(Handle *handle) override {
        return reinterpret_cast<ClockProHandle *>(handle)->value;
    }

    uint64_t new_id() override {
        id_mutex_.lock();
        uint64_t id = ++last_id_;
        id_mutex_.unlock();
        return id;
    }

    void prune() override {
        for (int s = 0; s < kNumShards; s++) {
            shard_[s].prune();
        }
    }

    size_t total_charge() const override {
        size_t total = 0;
        for (int s = 0; s < kNumShards; s++) {
            total += shard_[s].total_charge();
        }
        return total;
    }

  private:
    std::vector<ClockProCache> shard_;

    std::mutex id_mutex_;
    uint64_t last_id_;

    static inline uint64_t hash_slice(std::string_view s) {
        return xx_hash64(s);
    }

    static uint64_t shard(uint64_t hash) {
        return hash >> (64 - kNumShardBits);
    }
};

} // namespace anchordb
