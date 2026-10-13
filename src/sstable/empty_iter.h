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
#include <cassert>
#include <string_view>

namespace anchordb {

class EmptyIterator {
  public:
    EmptyIterator() = default;
    EmptyIterator(const Status &s) : status_(s) {}
    ~EmptyIterator() = default;

    EmptyIterator(const EmptyIterator &other) : status_(other.status_) {}
    EmptyIterator &operator=(const EmptyIterator &other) {
        if (this != &other) {
            status_ = other.status_;
        }
        return *this;
    }

    EmptyIterator(EmptyIterator &&other) noexcept
        : status_(std::move(other.status_)) {}
    EmptyIterator &operator=(EmptyIterator &&other) {
        if (this != &other) {
            this->status_ = std::move(other.status_);
        }
        return *this;
    }

    bool valid() const { return false; }
    void seek(std::string_view target) {}
    void seek_to_first() {}
    void seek_to_last() {}
    void next() { assert(false); }
    void prev() { assert(false); }
    std::string_view key() const {
        assert(false);
        return std::string_view();
    }
    std::string_view value() const {
        assert(false);
        return std::string_view();
    }
    Status status() const { return status_; }

  private:
    Status status_;
};

namespace details::EmptyIter {
inline bool valid(const void *state_ptr) {
    auto *state = static_cast<const EmptyIterator *>(state_ptr);
    return state->valid();
}

inline void next(void *state_ptr) {
    auto *state = static_cast<EmptyIterator *>(state_ptr);
    return state->next();
}

inline void prev(void *state_ptr) {
    auto *state = static_cast<EmptyIterator *>(state_ptr);
    return state->prev();
}

inline void seek(void *state_ptr, std::string_view key) {
    auto *state = static_cast<EmptyIterator *>(state_ptr);
    return state->seek(key);
}

inline void seek_to_first(void *state_ptr) {
    auto *state = static_cast<EmptyIterator *>(state_ptr);
    return state->seek_to_first();
}

inline void seek_to_last(void *state_ptr) {
    auto *state = static_cast<EmptyIterator *>(state_ptr);
    return state->seek_to_last();
}

inline std::string_view key(const void *state_ptr) {
    auto *state = static_cast<const EmptyIterator *>(state_ptr);
    return state->key();
}

inline std::string_view value(const void *state_ptr) {
    auto *state = static_cast<const EmptyIterator *>(state_ptr);
    return state->value();
}

inline Status status(const void *state_ptr) {
    auto *state = static_cast<const EmptyIterator *>(state_ptr);
    return state->status();
}

inline void destroy(void *state_ptr) {
    delete static_cast<EmptyIterator *>(state_ptr);
}
} // namespace details::EmptyIter

inline Iterator new_empty_iterator(EmptyIterator *it) {
    Iterator iterator;
    iterator.state = it;
    iterator.valid = &details::EmptyIter::valid;
    iterator.next = &details::EmptyIter::next;
    iterator.prev = &details::EmptyIter::prev;
    iterator.seek = &details::EmptyIter::seek;
    iterator.seek_to_first = &details::EmptyIter::seek_to_first;
    iterator.seek_to_last = &details::EmptyIter::seek_to_last;
    iterator.key = &details::EmptyIter::key;
    iterator.value = &details::EmptyIter::value;
    iterator.status = &details::EmptyIter::status;
    iterator.destroy = &details::EmptyIter::destroy;
    return iterator;
}

} // namespace anchordb
