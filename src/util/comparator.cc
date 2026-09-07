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

#include "util/comparator.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace anchordb {

const char *ByteWiseComparator::name() const {
    return "anchordb.ByteWiseComparator";
}

int ByteWiseComparator::compare(const Slice &a, const Slice &b) const {
    return a.compare(b);
}

void ByteWiseComparator::find_shortest_separator(std::string *start,
                                                 const Slice &limit) {
    size_t min_len = std::min(start->size(), limit.size());
    size_t diff_index = 0;
    while (diff_index < min_len && (*start)[diff_index] == limit[diff_index]) {
        diff_index++;
    }

    if (diff_index >= min_len) {
        // Do not shorten if one string is a prefix of the other
    } else {
        uint8_t diff_byte = static_cast<uint8_t>((*start)[diff_index]);
        if (diff_byte < static_cast<uint8_t>(0xff) &&
            diff_byte + 1 < static_cast<uint8_t>(limit[diff_index])) {
            (*start)[diff_index]++;
            start->resize(diff_index + 1);
            assert(compare(*start, limit) < 0);
        }
    }
}

void ByteWiseComparator::find_short_successor(std::string *key) const {
    size_t n = key->size();
    for (size_t i = 0; i < n; i++) {
        const uint8_t byte = (*key)[i];
        if (byte != static_cast<uint8_t>(0xff)) {
            (*key)[i] = byte + 1;
            key->resize(i + 1);
            return;
        }
    }
    // *key is a run of 0xffs.  Leave it alone.
}

} // namespace anchordb
