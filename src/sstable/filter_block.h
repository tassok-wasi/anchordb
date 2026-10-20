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

#include "anchordb/anchordb_namespace.h"
#include "anchordb/filter.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ANCHORDB_NAMESPACE {

// On-disk filter file format (little-endian):
//   | magic (4B "ANDF") | name_len (1B) | name (name_len B) |
//   | key_count (4B)    | filter bytes ... |
//
// The magic identifies the file; the filter name (from Filter::name) lets a
// reader detect a filter built with a different implementation than the one it
// was opened with; key_count lets a reader distinguish "no keys" from a
// truncated filter.  The trailing bytes are exactly what Filter::create_filter
// produced and what Filter::key_may_match expects.
inline constexpr char kFilterBlockMagic[4] = {'A', 'N', 'D', 'F'};
inline constexpr size_t kFilterBlockHeaderSize =
    sizeof(kFilterBlockMagic) + 1 +
    sizeof(uint32_t); // magic + name_len + count

// Builds the filter file for one SSTable from the table's user keys.
//
// Keys are buffered and, on finish(), a single membership filter is built over
// all of them.  Keys must be supplied in ascending order (the SSTable index
// order); consecutive duplicates are dropped, which keeps the filter compact
// because the index repeats a user key once per stored version.
//
// A builder created with a disabled Filter (null create_filter/name) stays
// disabled: it buffers nothing and finish() yields an empty buffer, so no
// filter file is written.
class FilterBlockBuilder {
  public:
    explicit FilterBlockBuilder(const Filter &filter);

    FilterBlockBuilder(const FilterBlockBuilder &) = delete;
    FilterBlockBuilder &operator=(const FilterBlockBuilder &) = delete;

    // True if the configured Filter can build a filter (create_filter and name
    // are both non-null).
    bool enabled() const { return enabled_; }

    // Adds one user key.  No-op when disabled.
    void add_key(std::string_view user_key);

    // Number of distinct keys collected (after dropping consecutive
    // duplicates).
    size_t key_count() const { return keys_.size(); }

    // Serializes the filter and returns the file contents.  Idempotent; returns
    // an empty string when disabled.
    const std::string &finish();

  private:
    Filter filter_;
    bool enabled_;
    std::vector<std::string> keys_;
    std::string last_key_;
    bool has_last_key_ = false;
    std::string block_;
    bool finished_ = false;
};

// Parses a filter file produced by FilterBlockBuilder and answers membership
// queries for a single table.  The reader borrows the filter bytes, which must
// outlive it.
class FilterBlockReader {
  public:
    // `block` must outlive the reader.  The reader is valid only if `block` is
    // well-formed and its embedded filter name matches `filter`.
    FilterBlockReader(const Filter &filter, std::string_view block);

    // True if the block is well-formed and matches the supplied Filter.
    bool valid() const { return valid_; }

    // Returns true if `user_key` may be present in the table, and false only if
    // it is definitely absent.  Conservative (returns true) when the reader is
    // invalid, so a missing or mismatched filter never hides a key.
    bool key_may_match(std::string_view user_key) const;

  private:
    Filter filter_;
    std::string_view filter_data_;
    uint32_t key_count_ = 0;
    bool valid_ = false;
};

} // namespace ANCHORDB_NAMESPACE
