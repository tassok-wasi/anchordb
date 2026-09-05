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

// Include Intrinsics based on architecture
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    #define HAS_X86 1
    #if defined(_MSC_VER)
        #include <intrin.h>
    #else
        #include <x86intrin.h>
    #endif
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define HAS_ARM 1
    #if defined(__ARM_FEATURE_CRC32)
        #include <arm_acle.h>
    #endif
#endif

namespace anchordb {
namespace crc32 {

uint32_t extend(uint32_t crc, const char *data, size_t n);

uint32_t value(const char *data, size_t n);

static const uint32_t mask_delta = 0xa282ead8ul; // used by leveldb

inline uint32_t mask(uint32_t crc) {
    return ((crc >> 15) | (crc << 17)) + mask_delta;
}

inline uint32_t unmask(uint32_t masked_crc) {
    uint32_t rot = masked_crc - mask_delta;
    return ((rot >> 17) | (rot << 15));
}

} // namespace crc32
} // namespace anchordb
