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
#include <malloc.h>

inline int alloc_aligned(void **ptr, size_t size, size_t alignment) {
    *ptr = _aligned_malloc(size, alignment);
    return *ptr == nullptr; // Returns 0 on success, 1 on failure
}
inline void free_aligned(void *ptr) { _aligned_free(ptr); }
