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

#include "anchordb/filter.h"
#include "bloom_filter.h"
#include "cuckoo_filter.h"
#include "ribbon_filter.h"
namespace anchordb {

// Dispatches key_may_match to the correct free function based on the filter
// type tag.  This avoids vtable overhead on the hot query path for built-in
// filters while still supporting user-defined filters through the virtual
// interface.
class FilterDispatch {
  public:
    FilterDispatch(FilterType t, const Filter *filter = nullptr) : type_(t) {
        if (type_ == FilterType::UserDefinedFilter) {
            user_defined_filter_ = filter;
        }
    }

    bool key_may_match(const Slice &key, const Slice &filter) const {
        switch (type_) {
        case FilterType::BuiltinBloomFilter:
            return bloom_key_may_match(key, filter);
        case FilterType::BuiltinCuckooFilter:
            return cuckoo_key_may_match(key, filter);
        case FilterType::BuiltinRibbonFilter:
            return ribbon_key_may_match(key, filter);
        case FilterType::UserDefinedFilter:
            // A null user-defined filter is treated conservatively (always
            // returns true — no false negatives).
            return user_defined_filter_ != nullptr
                       ? user_defined_filter_->key_may_match(key, filter)
                       : true;
        }
        // Unreachable — all FilterType values are covered above.  The return
        // silences compiler warnings.
        return true;
    }

  private:
    FilterType type_;
    const Filter *user_defined_filter_ = nullptr;
};

} // namespace anchordb
