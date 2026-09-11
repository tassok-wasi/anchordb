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
#include "util/bloom_filter.h"
namespace anchordb {

class FilterDispatch {
  public:
    FilterDispatch(FilterType t, const Filter *filter = nullptr) : type_(t) {
        if (type_ == FilterType::UserDefined && filter != nullptr) {
            user_defined_filter_ = filter;
        }
    }

    bool key_may_match(const Slice &key, const Slice &filter) const {
        switch (type_) {
        case FilterType::BuiltinBloom:
            return bloom_key_may_match(key, filter);
        case FilterType::BuiltinCuckoo:
            return false; // not implemented yet
        case FilterType::UserDefined:
            return user_defined_filter_->key_may_match(key, filter);
        }
    }

  private:
    FilterType type_;
    const Filter *user_defined_filter_ = nullptr;
};

} // namespace anchordb
