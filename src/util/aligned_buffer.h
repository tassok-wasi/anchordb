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

#include "port/port.h" // IWYU pragma: keep
#include "port/port_posix.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

inline size_t round_up(size_t x, size_t y) { return ((x + y - 1) / y) * y; }
inline size_t round_down(size_t x, size_t y) { return (x / y) * y; }

inline size_t truncate_to_page_boundary(size_t page_size, size_t s) {
    s -= (s & (page_size - 1));
    assert((s % page_size) == 0);
    return s;
}

class AlignedBuffer {
  public:
    AlignedBuffer()
        : bufstart_(nullptr), capacity_(0), cursize_(0), alignment_(4096) {}

    explicit AlignedBuffer(size_t capacity, size_t alignment = 4096)
        : bufstart_(nullptr), capacity_(round_up(capacity, alignment)),
          cursize_(0), alignment_(alignment) {
        allocate();
    }

    ~AlignedBuffer() { free(); }

    AlignedBuffer(const AlignedBuffer &) = delete;
    AlignedBuffer &operator=(const AlignedBuffer &) = delete;

    AlignedBuffer(AlignedBuffer &&other) noexcept
        : bufstart_(other.bufstart_), capacity_(other.capacity_),
          cursize_(other.cursize_), alignment_(other.alignment_) {
        other.bufstart_ = nullptr;
        other.capacity_ = 0;
        other.cursize_ = 0;
    }

    AlignedBuffer &operator=(AlignedBuffer &&other) noexcept {
        if (this != &other) {
            free();
            bufstart_ = other.bufstart_;
            capacity_ = other.capacity_;
            cursize_ = other.cursize_;
            alignment_ = other.alignment_;
            other.bufstart_ = nullptr;
            other.capacity_ = 0;
            other.cursize_ = 0;
        }
        return *this;
    }

    void re_allocate(size_t req_capacity, bool copy_data = false,
                     uint64_t copy_offset = 0, size_t copy_len = 0) {
        assert(alignment_ > 0);
        assert((alignment_ & (alignment_ - 1)) == 0);

        copy_len = copy_len > 0 ? copy_len : cursize_;
        if (copy_data && req_capacity < copy_len) {
            return;
        }
        size_t new_capacity = round_up(req_capacity, alignment_);

        void *raw_ptr = nullptr;
        if (alloc_aligned(&raw_ptr, new_capacity, alignment_) != 0) {
            bufstart_ = nullptr;
            capacity_ = 0;
            return;
        }
        char *new_bufstart = static_cast<char *>(raw_ptr);

        if (copy_data && bufstart_ != nullptr) {
            assert(bufstart_ + copy_offset + copy_len <= bufstart_ + cursize_);
            memcpy(new_bufstart, bufstart_ + copy_offset, copy_len);
            cursize_ = copy_len;
        } else {
            cursize_ = 0;
        }

        char *old_buf = bufstart_;
        capacity_ = new_capacity;
        bufstart_ = new_bufstart;

        if (old_buf) {
            free_aligned(static_cast<void *>(old_buf));
        }
    }

    size_t append(const char *src, size_t size) {
        size_t remaining = capacity_ - cursize_;
        size_t to_copy = std::min(size, remaining);

        if (to_copy > 0) {
            std::memcpy(bufstart_ + cursize_, src, to_copy);
            cursize_ += to_copy;
        }
        return to_copy;
    }

    size_t read(char *dest, size_t offset, size_t read_size) const {
        assert(offset < cursize_);

        size_t to_read = 0;
        if (offset < cursize_) {
            to_read = std::min(cursize_ - offset, read_size);
        }
        if (to_read > 0) {
            memcpy(dest, bufstart_ + offset, to_read);
        }
        return to_read;
    }

    char *data() const { return bufstart_; }
    char *dest() const { return bufstart_ + cursize_; }
    void size(size_t cursize) { cursize_ = cursize; }
    size_t capacity() const { return capacity_; }
    size_t current_size() const { return cursize_; }
    size_t alignment() const { return alignment_; }
    bool is_allocated() const { return bufstart_ != nullptr; }
    void clear() { cursize_ = 0; }

  private:
    char *bufstart_;
    size_t capacity_;
    size_t alignment_;
    size_t cursize_;

    void allocate() {
        assert(capacity_ != 0);
        assert(alignment_ > 0);
        assert((alignment_ & (alignment_ - 1)) == 0);

        void *raw_ptr = nullptr;
        if (alloc_aligned(&raw_ptr, alignment_, capacity_) != 0) {
            bufstart_ = nullptr;
            capacity_ = 0;
            return;
        }
        bufstart_ = static_cast<char *>(raw_ptr);
    }

    void free() {
        if (bufstart_) {
            free_aligned(bufstart_);
            bufstart_ = nullptr;
        }
        capacity_ = 0;
        cursize_ = 0;
    }
};

} // namespace anchordb
