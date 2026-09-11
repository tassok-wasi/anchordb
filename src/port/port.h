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
#include "port_posix.h"
#endif

// Include Intrinsics based on architecture
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||             \
    defined(_M_IX86)
#define HAS_X86 1
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <x86intrin.h>
#endif // _MSC_VER
#elif defined(__aarch64__) || defined(_M_ARM64)
#define HAS_ARM 1
#if defined(__ARM_FEATURE_CRC32)
#include <arm_acle.h>
#endif // __ARM_FEATURE_CRC32
#endif // _MSC_VER
