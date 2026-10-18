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

class IteratorWrapper {
  public:
    IteratorWrapper() : iter_(nullptr), valid_(false) {}
    explicit IteratorWrapper(Iterator *iter) : iter_(nullptr) { set(iter); }
    ~IteratorWrapper() { delete iter_; }
    Iterator *iter() const { return iter_; }

    void set(Iterator *iter) {
        delete iter_;
        iter_ = iter;
        if (iter_ == nullptr) {
            valid_ = false;
        } else {
            update();
        }
    }

    bool valid() { return valid_; }

    std::string_view key() {
        assert(valid());
        return key_;
    }

    std::string_view value() {
        assert(valid());
        return iter_->value();
    }

    Status status() {
        assert(iter_);
        return iter_->status();
    }

    void next() {
        assert(iter_);
        iter_->next();
        update();
    }

    void prev() {
        assert(iter_);
        iter_->prev();
        update();
    }

    void seek(std::string_view key) {
        assert(iter_);
        iter_->seek(key);
        update();
    }

    void seek_to_first() {
        assert(iter_);
        iter_->seek_to_first();
        update();
    }

    void seek_to_last() {
        assert(iter_);
        iter_->seek_to_last();
        update();
    }

  private:
    void update() {
        valid_ = iter_->valid();
        if (valid_) {
            key_ = iter_->key();
        }
    }

    Iterator *iter_;
    bool valid_;
    std::string_view key_;
};

} // namespace anchordb
