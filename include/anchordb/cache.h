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

#include "anchordb/export.h"
#include "anchordb/options.h"
#include "anchordb/slice.h"
#include <cstddef>
#include <cstdint>
namespace anchordb {

class ANCHORDB_EXPORT Cache {
  public:
    Cache() = default;

    Cache(const Cache &) = delete;
    Cache &operator=(const Cache &) = delete;

    virtual ~Cache();

    struct Handle {};

    // insert a mapping from key->value into the cache and assign it
    // the specified charge against the total cache capacity.
    //
    // Returns a handle that corresponds to the mapping.  The caller
    // must call this->release(handle) when the returned mapping is no
    // longer needed.
    //
    // When the inserted entry is no longer needed, the key and
    // value will be passed to "deleter".
    virtual Handle *insert(const Slice &key, void *value, size_t charge,
                           void (*deleter)(const Slice &key, void *value)) = 0;

    // If the cache has no mapping for "key", returns nullptr.
    //
    // Else return a handle that corresponds to the mapping.  The caller
    // must call this->release(handle) when the returned mapping is no
    // longer needed.
    virtual Handle *lookup(const Slice &key) = 0;

    // Release a mapping returned by a previous lookup().
    // REQUIRES: handle must not have been released yet.
    // REQUIRES: handle must have been returned by a method on *this.
    virtual void release(Handle *handle) = 0;

    // Return the value encapsulated in a handle returned by a
    // successful lookup().
    // REQUIRES: handle must not have been released yet.
    // REQUIRES: handle must have been returned by a method on *this.
    virtual void *value(Handle *handle) = 0;

    // If the cache contains entry for key, erase it.  Note that the
    // underlying entry will be kept around until all existing handles
    // to it have been released.
    virtual void erase(const Slice &key) = 0;

    // Return a new numeric id.  May be used by multiple clients who are
    // sharing the same cache to partition the key space.  Typically the
    // client will allocate a new id at startup and prepend the id to
    // its cache keys.
    virtual uint64_t new_id() = 0;

    // Remove all cache entries that are not actively in use. Memory-constrained
    // applications may wish to call this method to reduce memory usage.
    virtual void prune() = 0;

    // Return an estimate of the combined charges of all elements stored in the
    // cache.
    virtual size_t total_charge() const = 0;
};

ANCHORDB_EXPORT Cache *new_lru_cache(size_t capacity, MapType t);
ANCHORDB_EXPORT Cache *new_lirs_cache(size_t capacity, MapType t);
ANCHORDB_EXPORT Cache *new_clock_pro_cache(size_t capacity, MapType t);

} // namespace anchordb
