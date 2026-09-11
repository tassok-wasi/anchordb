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

// this configuration will be removed and system will use a single-writer
// multi-reader skiplist by default and there will be no need to define any of
// the above macros.
// `swmr_skiplist.h` is renamed to `skiplist.h` -> this file is being used in
// the memtable

// Count how many variants are actively defined as non-zero
#if defined(MWMR_MEMTABLE_SKIPLIST) && (MWMR_MEMTABLE_SKIPLIST + 0)
#define _ANCHORDB_MWMR 1
#else
#define _ANCHORDB_MWMR 0
#endif

#if defined(SWMR_MEMTABLE_SKIPLIST) && (SWMR_MEMTABLE_SKIPLIST + 0)
#define _ANCHORDB_SWMR 1
#else
#define _ANCHORDB_SWMR 0
#endif

#if defined(SWSR_MEMTABLE_SKIPLIST) && (SWSR_MEMTABLE_SKIPLIST + 0)
#define _ANCHORDB_SWSR 1
#else
#define _ANCHORDB_SWSR 0
#endif

// Check for multiple conflicting definitions
#if (_ANCHORDB_MWMR + _ANCHORDB_SWMR + _ANCHORDB_SWSR) > 1
#error                                                                         \
    "Only one of MWMR_MEMTABLE_SKIPLIST, SWMR_MEMTABLE_SKIPLIST, or SWSR_MEMTABLE_SKIPLIST can be defined!"
#endif

// Default to MWMR if no variant was chosen by CMake or user C++ code
#if (_ANCHORDB_MWMR + _ANCHORDB_SWMR + _ANCHORDB_SWSR) == 0
#define MWMR_MEMTABLE_SKIPLIST 1
#endif
