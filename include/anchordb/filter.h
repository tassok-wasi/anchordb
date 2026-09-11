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
#include <cstdint>
#include <string>
namespace anchordb {
class Slice;

enum ANCHORDB_EXPORT FilterType : uint8_t {
    BuiltinBloom = 0,
    BuiltinCuckoo = 1,
    UserDefined = 2,
};

class ANCHORDB_EXPORT Filter {
  public:
    virtual ~Filter();

    virtual const char *name() const = 0;

    virtual const FilterType get_type() const = 0;

    virtual void create_filter(const Slice *keys, int n,
                               std::string *dst) const = 0;

    // user must implement this method.
    // for internal types like `BuiltinBloom`, `BuiltinCuckoo` we don't
    // implement this method. we use internal implementations instead
    virtual bool key_may_match(const Slice &key, const Slice &filter) const;
};
} // namespace anchordb
