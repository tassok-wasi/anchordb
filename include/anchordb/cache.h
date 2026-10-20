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
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

// Abstract in-memory block cache.
//
// A Cache maps keys to caller-owned values and tracks a single `charge` per
// entry (typically the entry's approximate size in bytes) so implementations
// can bound total memory.  Values are opaque to the cache: it never inspects or
// frees them directly, calling the supplied `deleter` instead.
//
// Entry lifetime: insert()/lookup() return a Handle that pins the entry.  The
// caller must pass that handle to release() once done.  An entry is only
// reclaimed by the cache (or by erase()) after every outstanding handle has
// been released.
//
// Thread-safety: implementations are internally synchronized; a single Cache
// may be shared across threads.  A given Handle, however, is not thread-safe
// and must be released on the same Cache it came from.
//
// Obtain an instance via new_lru_cache(), new_lirs_cache() or
// new_clock_pro_cache().
class ANCHORDB_EXPORT Cache {
  public:
    Cache() = default;

    Cache(const Cache &) = delete;
    Cache &operator=(const Cache &) = delete;

    virtual ~Cache();

    // Opaque, implementation-defined token identifying one cached entry.  Treat
    // it as an opaque pointer; its layout is private to the implementation.
    struct Handle {};

    // Inserts the mapping key -> value, charging `charge` against the cache's
    // capacity, and returns a handle the caller must later release().
    //
    // If an entry for `key` already exists it is replaced.  `deleter` is
    // invoked with the key and value when the entry is finally evicted, erased
    // or destroyed; it is responsible for freeing `value`.
    virtual Handle *insert(std::string_view key, void *value, size_t charge,
                           void (*deleter)(std::string_view key,
                                           void *value)) = 0;

    // Returns a handle for the cached mapping of `key`, or nullptr on a miss.
    // The caller must release() the returned handle when it is no longer
    // needed.
    virtual Handle *lookup(std::string_view key) = 0;

    // Releases a handle previously returned by insert() or lookup().  The entry
    // becomes reclaimable once its last handle is released.
    // REQUIRES: `handle` has not already been released.
    virtual void release(Handle *handle) = 0;

    // Returns the value stored for a handle returned by insert()/lookup().
    // REQUIRES: `handle` has not already been released.
    virtual void *value(Handle *handle) = 0;

    // Removes the mapping for `key`, if present.  The entry itself is kept
    // alive until every outstanding handle to it has been released.
    virtual void erase(std::string_view key) = 0;

    // Returns a fresh numeric id, unique within this cache instance.  Clients
    // that share one cache can partition the key space by allocating an id up
    // front and prefixing it to their keys.
    virtual uint64_t new_id() = 0;

    // Discards entries that are not currently pinned by a handle, reducing
    // memory usage on memory-constrained machines.
    virtual void prune() = 0;

    // Returns the combined charge of all entries currently resident in the
    // cache.
    virtual size_t total_charge() const = 0;
};

// Creates a cache using the LRU replacement policy.  `capacity` bounds the sum
// of entry charges; `t` selects the hash-map backend used for the index.
ANCHORDB_EXPORT Cache *new_lru_cache(size_t capacity, MapType t);
// Creates a cache using the LIRS replacement policy (see src/cache/lirs.h).
ANCHORDB_EXPORT Cache *new_lirs_cache(size_t capacity, MapType t);
// Creates a cache using the CLOCK-Pro replacement policy (see
// src/cache/clock_pro.h).
ANCHORDB_EXPORT Cache *new_clock_pro_cache(size_t capacity, MapType t);
// Creates a cache using the ARC replacement policy (see src/cache/arc.h).
ANCHORDB_EXPORT Cache *new_arc_cache(size_t capacity, MapType t);

} // namespace ANCHORDB_NAMESPACE
