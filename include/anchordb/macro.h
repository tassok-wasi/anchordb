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

#if defined(MWMR_MEMTABLE_SKIPLIST) + defined(SWMR_MEMTABLE_SKIPLIST) +        \
        defined(SWSR_MEMTABLE_SKIPLIST) >                                      \
    1
#error                                                                         \
    "Only one of MWMR_MEMTABLE_SKIPLIST, SWMR_MEMTABLE_SKIPLIST, or SWSR_MEMTABLE_SKIPLIST can be defined"
#endif

#if !defined(MWMR_MEMTABLE_SKIPLIST) && !defined(SWMR_MEMTABLE_SKIPLIST) &&    \
    !defined(SWSR_MEMTABLE_SKIPLIST)
#define MWMR_MEMTABLE_SKIPLIST 1
#endif
