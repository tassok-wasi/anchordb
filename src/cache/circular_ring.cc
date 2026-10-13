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

#include "circular_ring.h"
#include <cassert>

namespace anchordb {

CircularRing::CircularRing(CircularRing &&other) noexcept
    : head_(other.head_), index_map_(std::move(other.index_map_)),
      size_(other.size_) {
    other.head_ = nullptr;
    other.size_ = 0;
}

CircularRing &CircularRing::operator=(CircularRing &&other) noexcept {
    if (this != &other) {
        clear();
        head_ = other.head_;
        index_map_ = std::move(other.index_map_);
        size_ = other.size_;

        other.head_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

void CircularRing::push_back(HashedKey key, ClockProHandle *handle) {
    assert(handle != nullptr);
    if (contains(key))
        return;

    if (is_empty()) {
        link_empty_(handle);
    } else {
        // In a circular ring, insert_before(head_) appends to the back of the
        // loop
        link_before_(head_, handle);
    }

    size_++;
    index_map_.insert(key, handle);
}

void CircularRing::insert_before(ClockProHandle *target,
                                 ClockProHandle *handle) {
    assert(target != nullptr);
    assert(handle != nullptr);
    HashedKey key{handle->key(), handle->hash};
    if (contains(key))
        return;

    link_before_(target, handle);
    size_++;
    index_map_.insert(key, handle);
}

void CircularRing::insert_after(ClockProHandle *target,
                                ClockProHandle *handle) {
    assert(target != nullptr);
    assert(handle != nullptr);
    HashedKey key{handle->key(), handle->hash};
    if (contains(key))
        return;

    link_after_(target, handle);
    size_++;
    index_map_.insert(key, handle);
}

void CircularRing::remove(ClockProHandle *handle) {
    if (handle == nullptr)
        return;
    HashedKey key{handle->key(), handle->hash};

    unlink_(handle);
    index_map_.remove(key);
    size_--;
}

void CircularRing::remove(HashedKey key) {
    ClockProHandle *target = index_map_.get(key);
    assert(target != nullptr);
    remove(target);
}

void CircularRing::clear() {
    if (head_ == nullptr)
        return;

    ClockProHandle *curr = head_;
    for (size_t i = 0; i < size_; ++i) {
        ClockProHandle *next = curr->next;
        curr->prev = nullptr;
        curr->next = nullptr;
        curr = next;
    }

    head_ = nullptr;
    size_ = 0;
    index_map_.clear();
}

void CircularRing::link_empty_(ClockProHandle *node) {
    assert(node != nullptr);
    head_ = node;
    node->next = node;
    node->prev = node;
}

void CircularRing::link_before_(ClockProHandle *target, ClockProHandle *node) {
    assert(target != nullptr);
    assert(node != nullptr);

    node->next = target;
    node->prev = target->prev;

    target->prev->next = node;
    target->prev = node;
}

void CircularRing::link_after_(ClockProHandle *target, ClockProHandle *node) {
    assert(target != nullptr);
    assert(node != nullptr);

    node->next = target->next;
    node->prev = target;

    target->next->prev = node;
    target->next = node;
}

void CircularRing::unlink_(ClockProHandle *node) {
    assert(node != nullptr);

    if (size_ == 1) {
        head_ = nullptr;
    } else {
        node->prev->next = node->next;
        node->next->prev = node->prev;

        if (node == head_) {
            head_ = node->next;
        }
    }

    node->prev = nullptr;
    node->next = nullptr;
}

} // namespace anchordb
