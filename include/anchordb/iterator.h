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

#include <anchordb/export.h>
#include <anchordb/slice.h>
#include <anchordb/status.h>

namespace anchordb {
class ANCHORDB_EXPORT Iterator {
  public:
    /// Constructs a new Iterator instance.
    Iterator();

    // Disallow copy construction and copy assignment.
    Iterator(const Iterator &) = delete;
    Iterator &operator=(const Iterator &) = delete;

    /// Destroys the Iterator, releasing any associated resources.
    virtual ~Iterator();

    /// Returns true if the iterator is positioned at a valid entry.
    virtual bool valid() const = 0;

    /// Advances the iterator to the next entry.
    virtual void next() = 0;

    /// Moves the iterator to the previous entry.
    virtual void prev() = 0;

    /// Positions the iterator at the first entry whose key is >= the given key.
    virtual void seek(const Slice &key) = 0;

    /// Positions the iterator at the first entry in the source.
    virtual void seek_to_first() = 0;

    /// Positions the iterator at the last entry in the source.
    virtual void seek_to_last() = 0;

    /// Returns the key of the entry at the current iterator position.
    /// The caller must ensure that valid() returns true before calling this
    /// method.
    virtual Slice key() const = 0;

    /// Returns the value of the entry at the current iterator position.
    /// The caller must ensure that valid() returns true before calling this
    /// method.
    virtual Slice value() const = 0;

    /// If an error has occurred, return it.  Else return an ok status.
    virtual Status status() const = 0;
};
} // namespace anchordb
