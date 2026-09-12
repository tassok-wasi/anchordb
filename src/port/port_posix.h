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
#include <cstdlib>
inline int alloc_aligned(void **ptr, size_t size, size_t alignment) {
    // posix_memalign returns 0 on success, or an error code on failure
    int result = ::posix_memalign(ptr, alignment, size);

    if (result != 0) {
        *ptr = nullptr; // Ensure ptr is null if allocation fails
        return 1;       // Return 1 to match Windows failure behavior
    }

    return 0; // Return 0 to match Windows success behavior
}
inline void free_aligned(void *ptr) { std::free(ptr); }

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||             \
    defined(_M_IX86)
#define HAS_X86 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#define HAS_ARM 1
#endif

#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(HAS_X86)
#include <x86intrin.h>
#endif

#if defined(__ARM_FEATURE_CRC32)
#include <arm_acle.h>
#endif
