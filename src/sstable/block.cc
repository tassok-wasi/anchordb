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

#include "anchordb/comparator.h"
#include "anchordb/iterator.h"
#include "anchordb/status.h"
#include "sstable/block.h"
#include "sstable/format.h"
#include "util/coding.h"
#include <cassert>
#include <cstdint>
#include <string_view>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

inline uint32_t Block::num_restarts() const {
    assert(size_ >= sizeof(uint32_t));
    return decode_fixed_32(data_ + size_ - sizeof(uint32_t));
}

Block::Block(const BlockContents &contents)
    : data_(contents.data.data()), size_(contents.data.size()),
      owned_(contents.heap_allocated) {
    if (size_ < sizeof(uint32_t)) {
        size_ = 0;
    } else {
        size_t max_restarts_allowed =
            (size_ - sizeof(uint32_t)) / sizeof(uint32_t);
        if (num_restarts() > max_restarts_allowed) {
            size_ = 0;
        } else {
            restart_offset_ = size_ - (1 + num_restarts()) * sizeof(uint32_t);
        }
    }
}

Block::~Block() {
    if (owned_) delete[] data_;
}

// Helper routine: decode the next block entry starting at "p",
// storing the number of shared key bytes, non_shared key bytes,
// and the length of the value in "*shared", "*non_shared", and
// "*value_length", respectively.  Will not dereference past "limit".
//
// If any errors are detected, returns nullptr.  Otherwise, returns a
// pointer to the key delta (just past the three decoded values).
static inline const char *decode_entry(const char *p, const char *limit,
                                       uint32_t *shared, uint32_t *non_shared,
                                       uint32_t *value_length) {
    if (limit - p < 3) return nullptr;
    *shared = reinterpret_cast<const uint8_t *>(p)[0];
    *non_shared = reinterpret_cast<const uint8_t *>(p)[1];
    *value_length = reinterpret_cast<const uint8_t *>(p)[2];
    if ((*shared | *non_shared | *value_length) < 128) {
        // Fast path: all three values are encoded in one byte each
        p += 3;
    } else {
        if ((p = get_varint_32_ptr(p, limit, shared)) == nullptr)
            return nullptr;
        if ((p = get_varint_32_ptr(p, limit, non_shared)) == nullptr)
            return nullptr;
        if ((p = get_varint_32_ptr(p, limit, value_length)) == nullptr)
            return nullptr;
    }

    if (static_cast<uint32_t>(limit - p) < (*non_shared + *value_length)) {
        return nullptr;
    }
    return p;
}

class Block::Iter : public Iterator {
  public:
    Iter(Comparator comparator, const char *data, uint32_t restarts,
         uint32_t num_restarts)
        : comparator_(comparator), data_(data), restarts_(restarts),
          num_restarts_(num_restarts), current_(restarts_),
          restart_index_(num_restarts_) {
        assert(num_restarts_ > 0);
    }

    bool valid() const override { return current_ < restarts_; }
    Status status() const override { return status_; }
    std::string_view key() const override {
        assert(valid());
        return key_;
    }
    std::string_view value() const override {
        assert(valid());
        return value_;
    }

    void next() override {
        assert(valid());
        parse_next_key();
    }

    void prev() override {
        assert(valid());

        // Scan backwards to a restart point before current_
        const uint32_t original = current_;
        while (get_restart_point(restart_index_) >= original) {
            if (restart_index_ == 0) {
                // No more entries
                current_ = restarts_;
                restart_index_ = num_restarts_;
                return;
            }
            restart_index_--;
        }

        seek_to_restart_point(restart_index_);
        do {
            // Loop until end of current entry hits the start of original entry
        } while (parse_next_key() && next_entry_offset() < original);
    }

    void seek(std::string_view target) override {
        // Binary search in restart array to find the last restart point
        // with a key < target
        uint32_t left = 0;
        uint32_t right = num_restarts_ - 1;
        int current_key_compare = 0;

        if (valid()) {
            // If we're already scanning, use the current position as a starting
            // point. This is beneficial if the key we're seeking to is ahead of
            // the current position.
            current_key_compare = compare(key_, target);
            if (current_key_compare < 0) {
                // key_ is smaller than target
                left = restart_index_;
            } else if (current_key_compare > 0) {
                right = restart_index_;
            } else {
                // We're seeking to the key we're already at.
                return;
            }
        }

        while (left < right) {
            uint32_t mid = (left + right + 1) / 2;
            uint32_t region_offset = get_restart_point(mid);
            uint32_t shared, non_shared, value_length;
            const char *key_ptr =
                decode_entry(data_ + region_offset, data_ + restarts_, &shared,
                             &non_shared, &value_length);
            if (key_ptr == nullptr || (shared != 0)) {
                corruption_error();
                return;
            }
            std::string_view mid_key(key_ptr, non_shared);
            if (compare(mid_key, target) < 0) {
                // Key at "mid" is smaller than "target".  Therefore all
                // blocks before "mid" are uninteresting.
                left = mid;
            } else {
                // Key at "mid" is >= "target".  Therefore all blocks at or
                // after "mid" are uninteresting.
                right = mid - 1;
            }
        }

        // We might be able to use our current position within the restart
        // block. This is true if we determined the key we desire is in the
        // current block and is after than the current key.
        assert(current_key_compare == 0 || valid());
        bool skip_seek = left == restart_index_ && current_key_compare < 0;
        if (!skip_seek) { seek_to_restart_point(left); }
        // Linear search (within restart block) for first key >= target
        while (true) {
            if (!parse_next_key()) { return; }
            if (compare(key_, target) >= 0) { return; }
        }
    }

    void seek_to_first() override {
        seek_to_restart_point(0);
        parse_next_key();
    }

    void seek_to_last() override {
        seek_to_restart_point(num_restarts_ - 1);
        while (parse_next_key() && next_entry_offset() < restarts_) {
            // Keep skipping
        }
    }

  private:
    inline int compare(std::string_view a, std::string_view b) const {
        return comparator_.compare(a, b);
    }

    // Return the offset in data_ just past the end of the current entry.
    inline uint32_t next_entry_offset() const {
        return (value_.data() + value_.size()) - data_;
    }

    uint32_t get_restart_point(uint32_t index) {
        assert(index < num_restarts_);
        return decode_fixed_32(data_ + restarts_ + index * sizeof(uint32_t));
    }

    void seek_to_restart_point(uint32_t index) {
        key_.clear();
        restart_index_ = index;
        // current_ will be fixed by parse_next_key();

        // parse_next_key() starts at the end of value_, so set value_
        // accordingly
        uint32_t offset = get_restart_point(index);
        value_ = std::string_view(data_ + offset, 0);
    }

    void corruption_error() {
        current_ = restarts_;
        restart_index_ = num_restarts_;
        status_ = Status::corruption("bad entry in block");
        key_.clear();
        value_ = ""; // essentially clearing the view
    }

    bool parse_next_key() {
        current_ = next_entry_offset();
        const char *p = data_ + current_;
        const char *limit = data_ + restarts_; // Restarts come right after data
        if (p >= limit) {
            // No more entries to return.  Mark as invalid.
            current_ = restarts_;
            restart_index_ = num_restarts_;
            return false;
        }

        // Decode next entry
        uint32_t shared, non_shared, value_length;
        p = decode_entry(p, limit, &shared, &non_shared, &value_length);
        if (p == nullptr || key_.size() < shared) {
            corruption_error();
            return false;
        } else {
            key_.resize(shared);
            key_.append(p, non_shared);
            value_ = std::string_view(p + non_shared, value_length);
            while (restart_index_ + 1 < num_restarts_ &&
                   get_restart_point(restart_index_ + 1) < current_) {
                ++restart_index_;
            }
            return true;
        }
    }

    const Comparator comparator_;
    const char *const data_;      // underlying block contents
    uint32_t const restarts_;     // Offset of restart array (list of fixed32)
    uint32_t const num_restarts_; // Number of uint32_t entries in restart array

    // current_ is offset in data_ of current entry.  >= restarts_ if !Valid
    uint32_t current_;
    uint32_t restart_index_; // Index of restart block in which current_ falls
    std::string key_;
    std::string_view value_;
    Status status_;
};

Iterator *Block::new_iterator(Comparator comparator) {
    if (size_ < sizeof(uint32_t)) {
        return new_error_iterator(Status::corruption("bad block contents"));
    }
    const uint32_t num_restarts_ = num_restarts();
    if (num_restarts_ == 0) {
        return new_empty_iterator();
    } else {
        return new Iter(comparator, data_, restart_offset_, num_restarts_);
    }
}

} // namespace anchordb
