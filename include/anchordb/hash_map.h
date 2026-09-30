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
#include "anchordb/slice.h"
#include <cstdint>

namespace anchordb {

struct ANCHORDB_EXPORT HashedKey {
    Slice key;
    uint64_t hash;
};

// Abstract base class for hash map implementations.
// Provides a common interface for different probing strategies
// (linear probing, Robin Hood, Swiss table, etc.).
class ANCHORDB_EXPORT Map {
  public:
    Map() = default;
    ~Map() = default;

    // Returns the name of the map implementation.
    virtual const char *name() const = 0;

    // Look up a key by its precomputed hash.
    // Returns true and sets out_value if the key is found, false otherwise.
    virtual bool find(const HashedKey &key, Slice &out_value) const = 0;

    // Insert or overwrite a key-value pair identified by its precomputed hash.
    virtual void insert(const HashedKey &key, const Slice &value) = 0;

    // Remove a key identified by its precomputed hash.
    // Returns true if the key was present, false otherwise.
    virtual bool erase(const HashedKey &key) = 0;

    virtual void clear() = 0;

    // Returns the number of key-value pairs currently stored in the map.
    virtual size_t size() const = 0;

    // Returns the total number of slots (including empty ones) in the map.
    virtual size_t capacity() const = 0;
};

// Factory functions for different hash map implementations.
// Each accepts an optional initial capacity (defaults to 16).

// Standard open-addressing map with linear probing.
Map *linear_probing_map(size_t initial_capacity = 16);

// Open-addressing map using Robin Hood hashing for improved probe variance.
Map *robin_hood_map(size_t initial_capacity = 16);

// High-performance map based on the Swiss table design (SIMD-accelerated
// probing).
Map *swiss_map(size_t initial_capacity = 16);

} // namespace anchordb
