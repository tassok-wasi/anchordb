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
#include <cstddef>
#include <cstdint>
#include <string>
namespace anchordb {
class Slice;

// Identifies the concrete filter implementation backing a serialized filter.
// The value is used by FilterDispatch to select the matching routine without
// paying vtable overhead on the hot query path.
enum class ANCHORDB_EXPORT FilterType : uint8_t {
    bloom = 0x0,
    cuckoo = 0x1,
    ribbon = 0x2,
    user_defined = 0x3,
};

// Abstract interface for probabilistic filters (Bloom, Cuckoo, Ribbon, or a
// user-supplied implementation).
//
// A filter answers the question "might this key be present?" with no false
// negatives: if key_may_match returns false the key is definitely absent, and
// if it returns true the key is only *probably* present.
class ANCHORDB_EXPORT Filter {
  public:
    virtual ~Filter() = default;

    // Human-readable, stable identifier for this filter implementation.
    virtual const char *name() const = 0;

    // The FilterType tag used to dispatch matching for serialized filters.
    virtual FilterType get_type() const = 0;

    // Serializes a filter for the given keys, appending the result to `dst`.
    // `keys` must be non-null when `n > 0`. The output is self-describing
    // (it encodes everything the matching routine needs to interpret it).
    virtual void create_filter(const Slice *keys, size_t n,
                               std::string *dst) const = 0;

    // Returns true if `key` may be present in `filter`.
    //
    // The default implementation conservatively returns true (never filters a
    // key out), which is always safe. Built-in filters (Bloom/Cuckoo/Ribbon)
    // intentionally do not override this: their matching logic lives in free
    // functions dispatched by FilterDispatch to avoid vtable overhead in the
    // hot path. User-defined filters must override this to provide real
    // matching.
    virtual bool key_may_match(const Slice &key, const Slice &filter) const {
        return true;
    }
};
} // namespace anchordb
