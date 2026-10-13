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

#include "anchordb/iterator.h"
#include "anchordb/status.h"
#include "table_util.h"
#include <string_view>

namespace anchordb {

class MemtableIterator {
  public:
    explicit MemtableIterator(Table *table) : iter_(table) {}

    MemtableIterator(const MemtableIterator &other)
        : iter_(other.iter_), tmp_(other.tmp_) {}
    MemtableIterator &operator=(const MemtableIterator &other) {
        if (this != &other) {
            iter_ = other.iter_;
            tmp_ = other.tmp_;
        }
        return *this;
    }

    MemtableIterator(MemtableIterator &&other) noexcept
        : iter_(std::move(other.iter_)), tmp_(std::move(other.tmp_)) {}

    MemtableIterator &operator=(MemtableIterator &&other) {
        if (this != &other) {
            this->iter_ = std::move(other.iter_);
            this->tmp_ = std::move(other.tmp_);
        }
        return *this;
    }

    ~MemtableIterator() = default;

    bool valid() const { return iter_.valid(); }
    void next() { iter_.next(); }
    void prev() { iter_.prev(); }
    void seek(std::string_view key) { iter_.seek(encode_key(&tmp_, key)); }
    void seek_to_first() { iter_.seek_to_first(); }
    void seek_to_last() { iter_.seek_to_last(); }
    std::string_view key() const { return get_len_prefixed_slice(iter_.key()); }
    std::string_view value() const {
        std::string_view key_slice = get_len_prefixed_slice(iter_.key());
        return get_len_prefixed_slice(key_slice.data() + key_slice.size());
    }
    Status status() const { return Status::ok(); }

  private:
    typename Table::Iterator iter_;
    std::string tmp_;
};

namespace details::MemIter {
inline bool valid(const void *state_ptr) {
    auto *state = static_cast<const MemtableIterator *>(state_ptr);
    return state->valid();
}

inline void next(void *state_ptr) {
    auto *state = static_cast<MemtableIterator *>(state_ptr);
    return state->next();
}

inline void prev(void *state_ptr) {
    auto *state = static_cast<MemtableIterator *>(state_ptr);
    return state->prev();
}

inline void seek(void *state_ptr, std::string_view key) {
    auto *state = static_cast<MemtableIterator *>(state_ptr);
    return state->seek(key);
}

inline void seek_to_first(void *state_ptr) {
    auto *state = static_cast<MemtableIterator *>(state_ptr);
    return state->seek_to_first();
}

inline void seek_to_last(void *state_ptr) {
    auto *state = static_cast<MemtableIterator *>(state_ptr);
    return state->seek_to_last();
}

inline std::string_view key(const void *state_ptr) {
    auto *state = static_cast<const MemtableIterator *>(state_ptr);
    return state->key();
}

inline std::string_view value(const void *state_ptr) {
    auto *state = static_cast<const MemtableIterator *>(state_ptr);
    return state->value();
}

inline Status status(const void *state_ptr) {
    auto *state = static_cast<const MemtableIterator *>(state_ptr);
    return state->status();
}

inline void destroy(void *state_ptr) {
    delete static_cast<MemtableIterator *>(state_ptr);
}
} // namespace details::MemIter

inline Iterator new_memtable_iterator(MemtableIterator *it) {
    Iterator iterator;
    iterator.state = it;
    iterator.valid = &details::MemIter::valid;
    iterator.next = &details::MemIter::next;
    iterator.prev = &details::MemIter::prev;
    iterator.seek = &details::MemIter::seek;
    iterator.seek_to_first = &details::MemIter::seek_to_first;
    iterator.seek_to_last = &details::MemIter::seek_to_last;
    iterator.key = &details::MemIter::key;
    iterator.value = &details::MemIter::value;
    iterator.status = &details::MemIter::status;
    iterator.destroy = &details::MemIter::destroy;
    return iterator;
}

} // namespace anchordb
