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

// A bidirectional iterator over an ordered key-value store.
//
// Keys are visited in ascending order.  After seeking, the iterator may be at
// an invalid position (valid() == false), so always check valid() before
// reading key()/value().  An iterator is not thread-safe: a single iterator may
// not be used concurrently from multiple threads.  The iterator owns the
// returned key/value storage, so those views are valid only until the iterator
// is repositioned or destroyed.
class ANCHORDB_EXPORT Iterator {
  public:
    Iterator();

    // Iterators are not copyable; obtain one from the owning store or from the
    // factory functions below.
    Iterator(const Iterator &) = delete;
    Iterator &operator=(const Iterator &) = delete;

    virtual ~Iterator();

    // Returns true if the iterator currently points at a valid entry.
    virtual bool valid() const = 0;
    // Positions at the first entry, or invalidates the iterator if empty.
    virtual void seek_to_first() = 0;
    // Positions at the last entry, or invalidates the iterator if empty.
    virtual void seek_to_last() = 0;
    // Positions at the first key >= target, or invalidates the iterator if
    // there is none.
    virtual void seek(std::string_view target) = 0;
    // Advances to the next entry; invalidates the iterator past the last.
    // REQUIRES: valid() is true.
    virtual void next() = 0;
    // Moves to the previous entry; invalidates the iterator before the first.
    // REQUIRES: valid() is true.
    virtual void prev() = 0;
    // Returns the current key.  REQUIRES: valid() is true.
    virtual std::string_view key() const = 0;
    // Returns the current value.  REQUIRES: valid() is true.
    virtual std::string_view value() const = 0;
    // Returns an error Status if iteration encountered a problem, or a
    // success Status otherwise.
    virtual Status status() const = 0;
};

// Creates an iterator over an empty range (always invalid, success status).
Iterator *new_empty_iterator();
// Creates an iterator that is always invalid and reports `status` from
// status().
Iterator *new_error_iterator(const Status &status);

} // namespace ANCHORDB_NAMESPACE
