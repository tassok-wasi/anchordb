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

#include "anchordb/comparator.h"
#include "memtable/skiplist.h"
#include "util/coding.h"
#include <cstdint>
#include <string_view>

namespace anchordb {

namespace {
std::string_view get_len_prefixed_slice(const char *data) {
    uint32_t len;
    const char *p = data;
    p = get_varint_32_ptr(p, p + 5, &len);
    return std::string_view(p, len);
}

char *encode_key(std::string *scratch, std::string_view target) {
    scratch->clear();
    put_varint_32(scratch, target.size());
    scratch->append(target.data(), target.size());
    return scratch->data();
}

struct KeyComparator {
    const Comparator comparator;
    explicit KeyComparator(const Comparator &c) : comparator(c) {}
    int operator()(const char *aptr, const char *bptr) const {
        std::string_view a = get_len_prefixed_slice(aptr);
        std::string_view b = get_len_prefixed_slice(bptr);
        return comparator.compare(a, b);
    }
};

} // namespace

using Table = Skiplist<const char *, KeyComparator>;

} // namespace anchordb
