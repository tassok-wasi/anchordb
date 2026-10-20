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
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

// Owning, non-virtual facade over a Cache: forwards each operation and deletes
// the wrapped cache on destruction.  Holding it by value keeps forwarding
// inlinable, leaving one virtual dispatch per call.
class CacheWrapper {
  public:
    explicit CacheWrapper(Cache *cache) : cache_(cache) {}

    CacheWrapper(const CacheWrapper &) = delete;
    CacheWrapper &operator=(const CacheWrapper &) = delete;

    CacheWrapper(CacheWrapper &&other) noexcept : cache_(other.cache_) {
        other.cache_ = nullptr;
    }
    CacheWrapper &operator=(CacheWrapper &&other) noexcept {
        if (this != &other) {
            delete cache_;
            cache_ = other.cache_;
            other.cache_ = nullptr;
        }
        return *this;
    }

    ~CacheWrapper() { delete cache_; }

    bool valid() const { return cache_ != nullptr; }

    Cache::Handle *insert(std::string_view key, void *value, size_t charge,
                          void (*deleter)(std::string_view key, void *value)) {
        return cache_->insert(key, value, charge, deleter);
    }

    Cache::Handle *lookup(std::string_view key) { return cache_->lookup(key); }

    void release(Cache::Handle *handle) { cache_->release(handle); }

    void *value(Cache::Handle *handle) { return cache_->value(handle); }

    void erase(std::string_view key) { cache_->erase(key); }

    uint64_t new_id() { return cache_->new_id(); }

    void prune() { cache_->prune(); }

    size_t total_charge() const { return cache_->total_charge(); }

  private:
    Cache *cache_;
};

} // namespace ANCHORDB_NAMESPACE
