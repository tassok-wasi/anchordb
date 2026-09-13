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
#include <anchordb/macro.h>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <string>
namespace anchordb {

// A non-owning, read-only view over a contiguous range of bytes.
// Slice is cheap to copy (two words: pointer + length) and does not manage
// the lifetime of the underlying data.  It is the primary currency for passing
// keys and values throughout the system.
//
// Comparison operators implement lexicographic bytewise ordering, consistent
// with memcmp semantics.
class ANCHORDB_EXPORT Slice {
  public:
    // Creates an empty slice (nullptr, 0).
    Slice() : data_(nullptr), size_(0) {}

    // Creates a slice referring to the first `size` bytes starting at `data`.
    Slice(const char *data, size_t size) : data_(data), size_(size) {}

    // Creates a slice referring to the entire contents of `str`.
    // The slice remains valid only as long as `str` is not mutated or
    // destroyed.
    Slice(const std::string &str) : data_(str.data()), size_(str.size()) {}

    // Creates a slice referring to the null-terminated C string `s`
    // (excluding the null terminator).
    Slice(const char *s) : data_(s), size_(strlen(s)) {}

    Slice(const Slice &) = default;
    Slice &operator=(const Slice &) = default;

    // Returns a pointer to the first byte of the slice.
    const char *data() const { return data_; }
    // Returns the number of bytes in the slice.
    size_t size() const { return size_; }

    // Returns true if the slice has zero length.
    bool empty() const { return size_ == 0; }

    // Returns the byte at index n.  Asserts that n < size().
    char operator[](size_t n) const {
        assert(n < size());
        return data_[n];
    }

    // Resets the slice to empty.
    void clear() {
        data_ = nullptr;
        size_ = 0;
    }

    // Advances the start pointer forward by n bytes, shrinking the slice.
    // Asserts that n <= size().
    void remove_prefix(size_t n) {
        assert(n <= size());
        data_ += n;
        size_ -= n;
    }

    // Truncates the slice by n bytes from the end.
    // Asserts that n <= size().
    void remove_suffix(size_t n) {
        assert(n <= size());
        size_ -= n;
    }

    // Returns a copy of the slice's contents as a std::string.
    std::string str() const { return std::string(data_, size_); }

    // Returns true if this slice begins with `prefix`.
    bool starts_with(const Slice &prefix) const {
        return size_ >= prefix.size_ &&
               std::memcmp(data_, prefix.data_, prefix.size_) == 0;
    }

    // Returns true if this slice ends with `suffix`.
    bool ends_with(const Slice &suffix) const {
        return size_ >= suffix.size_ &&
               std::memcmp(data_ + size_ - suffix.size_, suffix.data_,
                           suffix.size_) == 0;
    }

    int compare(const Slice &other) const { return compare_(other); }

    bool operator==(const Slice &other) const {
        return (size_ == other.size_) &&
               (std::memcmp(data_, other.data_, size_) == 0);
    }
    bool operator!=(const Slice &other) const { return !(*this == other); }

    bool operator<(const Slice &other) const { return compare_(other) < 0; }
    bool operator<=(const Slice &other) const { return compare_(other) <= 0; }
    bool operator>(const Slice &other) const { return compare_(other) > 0; }
    bool operator>=(const Slice &other) const { return compare_(other) >= 0; }

  private:
    const char *data_;
    size_t size_;

    // Lexicographic comparison via memcmp.  Returns negative, zero, or positive
    // as *this is less than, equal to, or greater than `other`.
    int compare_(const Slice &other) const {
        const size_t min_len = (size_ < other.size_) ? size_ : other.size_;
        int r = std::memcmp(data_, other.data_, min_len);
        if (r == 0) {
            if (size_ < other.size_)
                r = -1;
            else if (size_ > other.size_)
                r = 1;
        }
        return r;
    }
};
} // namespace anchordb
