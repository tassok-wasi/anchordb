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

#include "swiss_map.h"
#include "util.h"
#include <cassert>
#include <cstddef>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

class CircularRing {
  public:
    CircularRing() = default;
    ~CircularRing() { clear(); }

    CircularRing(const CircularRing &) = delete;
    CircularRing &operator=(const CircularRing &) = delete;

    CircularRing(CircularRing &&other) noexcept;
    CircularRing &operator=(CircularRing &&other) noexcept;

    bool is_empty() const { return head_ == nullptr; }
    size_t size() const { return size_; }

    bool contains(HashedKey key) const { return index_map_.contains(key); }

    ClockProHandle *get(HashedKey key) const { return index_map_.get(key); }

    // Circular insertion primitives
    void push_back(HashedKey key, ClockProHandle *handle);
    void insert_before(ClockProHandle *target, ClockProHandle *handle);
    void insert_after(ClockProHandle *target, ClockProHandle *handle);

    // Unlinks from circular ring and map index without freeing handle memory
    void remove(ClockProHandle *handle);
    void remove(HashedKey key);

    ClockProHandle *head() const { return head_; }

    // Advances clock hand pointer by 1 step in O(1) time
    ClockProHandle *step(ClockProHandle *hand) const {
        if (hand == nullptr)
            return head_;
        return hand->next;
    }

    void clear();

  private:
    ClockProHandle *head_{nullptr};
    SwissMap<ClockProHandle *> index_map_;
    size_t size_{0};

    void link_empty_(ClockProHandle *node);
    void link_before_(ClockProHandle *target, ClockProHandle *node);
    void link_after_(ClockProHandle *target, ClockProHandle *node);
    void unlink_(ClockProHandle *node);
};

} // namespace anchordb
