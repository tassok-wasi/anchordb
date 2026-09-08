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

#include "anchordb/env.h"
#include <cstddef>
#if defined(_WIN32)
#include <malloc.h>
#endif
namespace anchordb {

class DefaultAlignedBuffer final : public AlignedBuffer {
  public:
    DefaultAlignedBuffer() : ptr_(nullptr), capacity_(0), alignment_(4096) {}

    explicit DefaultAlignedBuffer(size_t capacity, size_t alignment = 4096)
        : ptr_(nullptr), capacity_(0), alignment_(alignment) {
        allocate(capacity, alignment);
    }

    ~DefaultAlignedBuffer() override {}

    DefaultAlignedBuffer(DefaultAlignedBuffer &&other) noexcept
        : ptr_(other.ptr_), capacity_(other.capacity_),
          alignment_(other.alignment_) {
        other.ptr_ = nullptr;
        other.capacity_ = 0;
    }

    DefaultAlignedBuffer &operator=(DefaultAlignedBuffer &&other) noexcept {
        if (this != &other) {
            free();
            ptr_ = other.ptr_;
            capacity_ = other.capacity_;
            alignment_ = other.alignment_;
            other.ptr_ = nullptr;
            other.capacity_ = 0;
        }
        return *this;
    }

    void allocate(size_t capacity, size_t alignment) override {
        if (ptr_ && capacity_ >= capacity && alignment_ == alignment) {
            return; // Reusable path: keep existing heap memory
        }

        free();
        if (capacity == 0)
            return;

        alignment_ = alignment;
        size_t remainder = capacity % alignment_;
        capacity_ =
            (remainder == 0) ? capacity : (capacity + alignment_ - remainder);

#if defined(_WIN32)
        ptr_ = static_cast<char *>(_aligned_malloc(capacity_, alignment_));
#else
        void *raw_ptr = nullptr;
        if (::posix_memalign(&raw_ptr, alignment_, capacity_) != 0) {
            ptr_ = nullptr;
            capacity_ = 0;
            return;
        }
        ptr_ = static_cast<char *>(raw_ptr);
#endif
    }
    void free() override {
        if (ptr_) {
#if defined(_WIN32)
            _aligned_free(ptr_);
#else
            ::free(ptr_);
#endif
            ptr_ = nullptr;
        }
        capacity_ = 0;
    }

    char *data() const override { return ptr_; }
    size_t capacity() const override { return capacity_; }
    size_t alignment() const override { return alignment_; }
    bool is_allocated() const override { return ptr_ != nullptr; }

  private:
    char *ptr_;
    size_t capacity_;
    size_t alignment_;
};

} // namespace anchordb
