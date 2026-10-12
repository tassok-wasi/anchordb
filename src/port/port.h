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

#if defined(_WIN32)
    #include "port_win.h"
#else
    #include "port_posix.h" // IWYU pragma: keep
#endif

#include "port_stdcxx.h" // IWYU pragma: keep

#if defined(_MSC_VER)
    #include <intrin.h>
#elif defined(HAS_X86)
    #include <x86intrin.h>
#endif

#if defined(__ARM_FEATURE_CRC32)
    #include <arm_acle.h>
#endif

#if defined(HAS_X86)
    #include <immintrin.h>
#elif defined(HAS_ARM)
    #include <arm_neon.h>
#endif

#if HAS_SNAPPY
    #include <snappy.h>
#endif // HAVE_SNAPPY
#if HAS_ZSTD
    #include <algorithm>
    #include <zstd.h>
#endif // HAVE_ZSTD
