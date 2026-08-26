#pragma once

#include "export.h"
#include "macro.h"
#include <cassert>
#include <cstddef>
#include <cstring>
#include <string>
namespace anchordb {
class ANCHORDB_EXPORT Slice {
  public:
    // default constructor, creates an empty slice
    Slice() : data_(nullptr), size_(0) {}

    // create a slice that refers to data[0, size-1]
    Slice(const char *data, size_t size) : data_(data), size_(size) {}

    // create a slice that refers to the contents of the given string
    Slice(const std::string &str) : data_(str.data()), size_(str.size()) {}

    // create a slice that refers to the given C string
    Slice(const char *s) : data_(s), size_(strlen(s)) {}

    Slice(const Slice &) = default;
    Slice &operator=(const Slice &) = default;

    const char *data() const { return data_; }
    size_t size() const { return size_; }

    bool empty() const { return size_ == 0; }

    const char *begin() const { return data_; }
    const char *end() const { return data_ + size_; }

    char operator[](size_t n) const {
        assert(n < size());
        return data_[n];
    }

    void clear() {
        data_ = nullptr;
        size_ = 0;
    }

    void remove_prefix(size_t n) {
        assert(n <= size());
        data_ += n;
        size_ -= n;
    }

    void remove_suffix(size_t n) {
        assert(n <= size());
        size_ -= n;
    }

    std::string str() const { return std::string(data_, size_); }

    bool starts_with(const Slice &prefix) const {
        return size_ >= prefix.size_ &&
               std::memcmp(data_, prefix.data_, prefix.size_) == 0;
    }

    bool ends_with(const Slice &suffix) const {
        return size_ >= suffix.size_ &&
               std::memcmp(data_ + size_ - suffix.size_, suffix.data_,
                           suffix.size_) == 0;
    }

    // Compare two slices lexicographically
    bool operator<(const Slice &other) const { return compare_(other) < 0; }
    bool operator>(const Slice &other) const { return compare_(other) > 0; }
    bool operator<=(const Slice &other) const { return compare_(other) <= 0; }
    bool operator>=(const Slice &other) const { return compare_(other) >= 0; }
    bool operator==(const Slice &other) const {
        return size_ == other.size_ &&
               std::memcmp(data_, other.data_, size_) == 0;
    }
    bool operator!=(const Slice &other) const { return !(*this == other); }

  private:
    const char *data_;
    size_t size_;

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
