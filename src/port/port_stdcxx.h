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

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||             \
    defined(_M_IX86)
    #define HAS_X86 1
#elif defined(__aarch64__) || defined(_M_ARM64) || defined(__arm__) ||         \
    defined(_M_ARM)
    #define HAS_ARM 1
#endif

// Define to 1 if you have a definition for fdatasync() in <unistd.h>.
#if !defined(HAS_FDATASYNC)
    #define HAS_FDATASYNC 1
#endif // !defined(HAS_FDATASYNC)

// Define to 1 if you have a definition for F_FULLFSYNC in <fcntl.h>.
#if !defined(HAS_FULLFSYNC)
    #define HAVE_FULLFSYNC 0
#endif // !defined(HAS_FULLFSYNC)

// Define to 1 if you have a definition for O_CLOEXEC in <fcntl.h>.
#if !defined(HAS_O_CLOEXEC)
    #define HAS_O_CLOEXEC 1
#endif // !defined(HAS_O_CLOEXEC)

// Define to 1 if you have Google Snappy.
#if !defined(HAS_SNAPPY)
    #define HAS_SNAPPY 0
#endif // !defined(HAS_SNAPPY)

// Define to 1 if you have Zstd.
#if !defined(HAS_ZSTD)
    #define HAS_ZSTD 0
#endif // !defined(HAS_ZSTD)

#if !defined(HAS_LZ4)
    #define HAS_LZ4 0
#endif // !defined(HAS_LZ4)

#if !defined(HAS_ZLIB)
    #define HAS_ZLIB 0
#endif // !defined(HAS_ZLIB)
