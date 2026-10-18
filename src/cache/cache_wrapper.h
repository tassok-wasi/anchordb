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
#include <memory>
#include <string_view>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

// Owns a concrete Cache implementation and exposes its operations through a
// non-virtual facade.  Callers hold a CacheWrapper by value (or by a single
// pointer) so that forwarding is inlinable and there is exactly one virtual
// dispatch (inside the wrapped Cache) per call instead of an extra indirection
// through a user-defined interface.  The wrapper also centralizes ownership so
// the wrapped cache is destroyed with the wrapper.
class CacheWrapper {
  public:
    explicit CacheWrapper(std::unique_ptr<Cache> cache)
        : cache_(std::move(cache)) {}

    CacheWrapper(const CacheWrapper &) = delete;
    CacheWrapper &operator=(const CacheWrapper &) = delete;

    CacheWrapper(CacheWrapper &&) noexcept = default;
    CacheWrapper &operator=(CacheWrapper &&) noexcept = default;

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
    std::unique_ptr<Cache> cache_;
};

} // namespace ANCHORDB_NAMESPACE
