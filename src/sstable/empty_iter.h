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

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

class EmptyIterator : public Iterator {
  public:
    EmptyIterator() = default;
    EmptyIterator(const Status &s) : status_(s) {}
    ~EmptyIterator() = default;

    EmptyIterator(EmptyIterator &&other) noexcept
        : status_(std::move(other.status_)) {}
    EmptyIterator &operator=(EmptyIterator &&other) {
        if (this != &other) {
            this->status_ = std::move(other.status_);
        }
        return *this;
    }

    bool valid() const override { return false; }
    void seek(std::string_view target) override {}
    void seek_to_first() override {}
    void seek_to_last() override {}
    void next() override { assert(false); }
    void prev() override { assert(false); }
    std::string_view key() const override {
        assert(false);
        return std::string_view();
    }
    std::string_view value() const override {
        assert(false);
        return std::string_view();
    }
    Status status() const override { return status_; }

  private:
    Status status_;
};

inline Iterator *new_empty_iterator() {
    return new EmptyIterator(Status::ok());
}
inline Iterator *new_error_iterator(const Status &status) {
    return new EmptyIterator(status);
}

} // namespace anchordb
