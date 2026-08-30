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

#include <anchordb/macro.h>

#if defined(MWMR_MEMTABLE_SKIPLIST)
#include "mwmr_skiplist.h"
#elif defined(SWMR_MEMTABLE_SKIPLIST)
#include "swmr_skiplist.h"
#elif defined(SWSR_MEMTABLE_SKIPLIST)
#include "swsr_skiplist.h"
#else
#error                                                                         \
    "No memtable skiplist implementation selected. Define one of: MWMR_MEMTABLE_SKIPLIST, SWMR_MEMTABLE_SKIPLIST, or SWSR_MEMTABLE_SKIPLIST"
#endif

namespace anchordb {

#if defined(MWMR_MEMTABLE_SKIPLIST)
template <typename Key, class Comparator>
using Skiplist = Skiplist_MWMR<Key, Comparator>;
#elif defined(SWMR_MEMTABLE_SKIPLIST)
template <typename Key, class Comparator>
using Skiplist = Skiplist_SWMR<Key, Comparator>;
#elif defined(SWSR_MEMTABLE_SKIPLIST)
template <typename Key, class Comparator>
using Skiplist = Skiplist_SWSR<Key, Comparator>;
#endif

} // namespace anchordb
