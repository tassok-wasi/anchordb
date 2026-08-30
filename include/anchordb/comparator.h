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

#include <anchordb/export.h>
#include <anchordb/slice.h>

namespace anchordb {
// Abstract interface for key ordering.  Implementations define a total order
// over Slices, which is used by the memtable, SSTables, and the internal key
// comparator to sort and search keys.
//
// Subclasses must implement compare() (returning negative, zero, or positive
// as a < b, a == b, or a > b) and name() (returning a short, persistent
// identifier for the comparator, used in SSTable metadata).
class ANCHORDB_EXPORT Comparator {
  public:
    virtual ~Comparator();

    // Compares two keys.  Returns:
    //   < 0  if a < b
    //   == 0 if a == b
    //   > 0  if a > b
    virtual int compare(const Slice &a, const Slice &b) const = 0;

    // Returns a human-readable name for this comparator (e.g.
    // "anchodb.BytewiseComparator"). The name is persisted in SSTable metadata
    // so that the correct comparator can be used when reopening a database.
    virtual const char *name() const = 0;
};
} // namespace anchordb
