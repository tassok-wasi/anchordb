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
#include "anchordb/status.h"
#include <string_view>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

// A bidirectional iterator over an ordered key-value store. Keys are
// returned in ascending order, and the same iterator may not be used
// concurrently from multiple threads.
class ANCHORDB_EXPORT Iterator {
  public:
    Iterator();

    // Iterators are not copyable; use the factory functions below or
    // obtain one from the owning database.
    Iterator(const Iterator &) = delete;
    Iterator &operator=(const Iterator &) = delete;

    virtual ~Iterator();
    // Returns true if the iterator is currently positioned at a valid entry.
    virtual bool valid() const = 0;
    virtual void seek_to_first() = 0;
    virtual void seek_to_last() = 0;
    // Positions the iterator at the first key >= target.
    virtual void seek(std::string_view target) = 0;
    virtual void next() = 0;
    virtual void prev() = 0;
    // The following accessors require valid() to be true.
    virtual std::string_view key() const = 0;
    virtual std::string_view value() const = 0;
    virtual Status status() const = 0;
};

// Creates an iterator over an empty range.
Iterator *new_empty_iterator();
// Creates an iterator that immediately reports the given error via status().
Iterator *new_error_iterator(const Status &status);

} // namespace anchordb
