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

#include "arena.h"
#include <cassert>
#include <cstddef>

namespace anchordb {

static const int k_block_size = 4096;

Arena::Arena()
    : alloc_ptr_{nullptr}, alloc_bytes_remaining_{0}, memory_usage_{0} {}

Arena::~Arena() {
    for (size_t i = 0; i < blocks_.size(); ++i) {
        delete[] blocks_[i];
    }
}

char *Arena::allocate(size_t bytes) {
    assert(bytes > 0);
    if (bytes <= alloc_bytes_remaining_) {
        char *block = alloc_ptr_;
        alloc_ptr_ += bytes;
        alloc_bytes_remaining_ -= bytes;
        return block;
    }
    return allocate_fallback(bytes);
}

char *Arena::allocate_aligned(size_t bytes) {
    const int align = (sizeof(void *) > 8) ? sizeof(void *) : 8;
    static_assert((align & (align - 1)) == 0,
                  "pointer size should be a power of 2");

    size_t curr_mod = reinterpret_cast<uintptr_t>(alloc_ptr_) & (align - 1);
    size_t slop = (curr_mod == 0 ? 0 : align - curr_mod);
    size_t needed = bytes + slop;

    char *block;
    if (needed <= alloc_bytes_remaining_) {
        block = alloc_ptr_ + slop;
        alloc_ptr_ += needed;
        alloc_bytes_remaining_ -= needed;
    } else {
        block = allocate_fallback(bytes);
    }

    assert((reinterpret_cast<uintptr_t>(block) & (align - 1)) == 0);
    return block;
}

char *Arena::allocate_fallback(size_t bytes) {
    if (bytes > k_block_size / 4) {
        char *block = allocate_new_block(bytes);
        return block;
    }

    alloc_ptr_ = allocate_new_block(bytes);
    alloc_bytes_remaining_ = k_block_size;

    char *block = alloc_ptr_;
    alloc_ptr_ += bytes;
    alloc_bytes_remaining_ -= bytes;

    return block;
}

char *Arena::allocate_new_block(size_t block_bytes) {
    char *block = new char[block_bytes];
    blocks_.push_back(block);
    memory_usage_ += block_bytes + sizeof(char *);
    return block;
}

} // namespace anchordb
