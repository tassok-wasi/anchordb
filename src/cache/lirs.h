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

namespace anchordb {

class LIRSCache {
  public:
    explicit LIRSCache(size_t capacity = 0, MapType t = MapType::Swiss)
        : capacity_(capacity), usage_(0), lir_usage_(0) {

        // Allocate 95% capacity to LIR and remaining 5% to HIR
        lir_capacity_ = (capacity_ * 95) / 100;
        hir_capacity_ = capacity_ - lir_capacity_;

        switch (t) {
        case MapType::LinearProbing:
            map_.emplace<LinearProbingMap<LIRSHandle *>>(capacity_);
            break;
        case MapType::RobbinHood:
            map_.emplace<RobinHoodMap<LIRSHandle *>>(capacity_);
            break;
        case MapType::Swiss:
            map_.emplace<SwissMap<LIRSHandle *>>(capacity_);
        }
    }
    // Frees every entry still held by the cache. Callers must have released
    // all outstanding handles before destroying the cache.
    ~LIRSCache() { clear_all(); }

    LIRSCache(const LIRSCache &) = delete;
    LIRSCache &operator=(const LIRSCache &) = delete;

    LIRSCache(LIRSCache &&other) noexcept : capacity_(other.capacity_) {
        other.mutex_.lock();
        usage_ = other.usage_;
        lir_usage_ = other.lir_usage_;
        lir_capacity_ = other.lir_capacity_;
        hir_capacity_ = other.hir_capacity_;
        stack_ = std::move(other.stack_);
        hir_list_ = std::move(other.hir_list_);
        map_ = std::move(other.map_);
        other.mutex_.unlock();
    }

    LIRSCache &operator=(LIRSCache &&other) noexcept {
        if (this != &other) {
            this->mutex_.lock();
            other.mutex_.lock();

            capacity_ = other.capacity_;
            usage_ = other.usage_;
            lir_usage_ = other.lir_usage_;
            lir_capacity_ = other.lir_capacity_;
            hir_capacity_ = other.hir_capacity_;
            stack_ = std::move(other.stack_);
            hir_list_ = std::move(other.hir_list_);
            map_ = std::move(other.map_);

            this->mutex_.unlock();
            other.mutex_.unlock();
        }
        return *this;
    }

    void set_capacity(size_t capacity) {
        capacity_ = capacity;
        lir_capacity_ = (capacity_ * 95) / 100;
        hir_capacity_ = capacity_ - lir_capacity_;
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
    void clear_all();
    void prune_stack();
    void evict();
    void ref(LIRSHandle *e);
    void unref(LIRSHandle *e);
    bool finish_erase(LIRSHandle *e);

    size_t capacity_;
    size_t lir_capacity_;
    size_t hir_capacity_;
    mutable std::mutex mutex_;
    size_t usage_;
    size_t lir_usage_;

    List<LIRSHandle *> stack_;    // LIRS Stack S
    List<LIRSHandle *> hir_list_; // LIRS Queue Q (resident HIR blocks)

    std::variant<LinearProbingMap<LIRSHandle *>, RobinHoodMap<LIRSHandle *>,
                 SwissMap<LIRSHandle *>>
        map_;
};

class ShardedLIRSCache : public Cache {
  public:
    explicit ShardedLIRSCache(size_t capacity, MapType t) : last_id_(0) {
        shard_.reserve(kNumShards);
        const size_t per_shard = (capacity + (kNumShards - 1)) / kNumShards;
        for (int s = 0; s < kNumShards; ++s) {
            shard_.emplace_back(per_shard, t);
        }
    }

    ~ShardedLIRSCache() override {};

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
        LIRSHandle *h = reinterpret_cast<LIRSHandle *>(handle);
        shard_[shard(h->hash)].release(handle);
    }

    void erase(std::string_view key) override {
        const uint64_t hash = hash_slice(key);
        shard_[shard(hash)].erase(key, hash);
    }

    void *value(Handle *handle) override {
        return reinterpret_cast<LIRSHandle *>(handle)->value;
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
    std::vector<LIRSCache> shard_;

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
