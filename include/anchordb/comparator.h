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

#include "anchordb/export.h"
#include <string>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

struct ANCHORDB_EXPORT Comparator {
    int (*compare)(std::string_view a, std::string_view b) = nullptr;
    const char *(*name)() = nullptr;
    void (*find_shortest_separator)(std::string *start,
                                    std::string_view limit) = nullptr;
    void (*find_short_successor)(std::string *key) = nullptr;
};

inline ANCHORDB_EXPORT const Comparator new_byte_wise_comparator();

} // namespace anchordb
