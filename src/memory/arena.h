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

#include <cstddef>
#include <cstdint>
#include <vector>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

// Block-based bump ("arena") allocator.  Memory is handed out from large blocks
// and never freed individually; the whole arena is reclaimed by `reset()` or
// destruction, which makes it cheap to allocate many small, short-lived objects
// (e.g. memtable skiplist nodes).
//
// Blocks come from the global `operator new[]`; tcmalloc is linked into the
// final binary, so no per-call allocator wrapper is needed here.
class Arena {
  public:
    explicit Arena(const uint32_t block_size);

    ~Arena();

    Arena(const Arena &) = delete;
    Arena &operator=(const Arena &) = delete;

    // Allocates the requested number of bytes from the arena.
    char *allocate(size_t bytes);
    // Allocates the requested number of bytes with alignment.
    char *allocate_aligned(size_t bytes);

    // Returns the total memory used by the arena.
    size_t memory_usage() const { return memory_usage_; }

    // Releases all allocated blocks and resets the arena.
    void reset();

  private:
    // Allocates a new block when the current one lacks space.
    char *allocate_fallback(size_t bytes);
    // Allocates and tracks a new block of the given size.
    char *allocate_new_block(size_t block_bytes);

    char *alloc_ptr_; // Pointer to the next free byte in the current block.
    size_t alloc_bytes_remaining_; // Bytes remaining in the current block.
    std::vector<char *> blocks_;   // All blocks owned by the arena.
    const uint32_t k_block_size_ = 4096; // configured block size, in bytes

    size_t memory_usage_; // Total memory allocated by the arena.
};

} // namespace ANCHORDB_NAMESPACE
