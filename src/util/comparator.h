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

#include "anchordb/slice.h"

namespace anchordb {

// ByteWiseComparator provides a lexicographic byte-by-byte comparison
// for keys stored in the database. It implements the Comparator interface
// expected by the underlying storage engine.
class ByteWiseComparator {
  public:
    // Returns the name of this comparator for identification and debugging.
    const char *name() const;

    // Compares two slices byte-by-byte (lexicographically).
    // Returns a negative value if a < b, zero if a == b, positive if a > b.
    int compare(const Slice &a, const Slice &b) const;

    // Modifies *start to be a short string that is >= *start and < limit.
    // Used to reduce the size of internal index blocks.
    void find_shortest_separator(std::string *start, const Slice &limit);

    // Modifies *key to be a short string that is > *key.
    // Used to find the next possible key in the index.
    void find_short_successor(std::string *key) const;
};

} // namespace anchordb
