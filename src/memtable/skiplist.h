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

#include "memory/arena.h"
#include "util/random.h"
#include <atomic>
#include <cassert>
#include <cstddef>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

// Single-writer/multi-reader skiplist.  Nodes are arena-allocated; concurrent
// readers are supported, but the caller must serialize writers.
template <typename Key, class Cmp> class Skiplist {
  private:
    struct Node {
        const Key key_;

        std::atomic<Node *> prev_;
        std::atomic<Node *> forward_[1];

        Node(const Key &k, int lvl) : key_(k), prev_(nullptr) {}

        ~Node() = default;
    };

  public:
    explicit Skiplist(Cmp cmp, Arena *arena)
        : arena_(arena), cmp_(cmp), curr_height_{0}, rnd_(0xdeadbeef) {
        head_ = new_node_(0, k_max_height_);
        for (int i = 0; i <= k_max_height_; ++i) {
            head_->forward_[i].store(nullptr, std::memory_order_relaxed);
        }
    }

    ~Skiplist() { clear_(); }

    // Nodes are owned by the arena and the comparator may be non-assignable,
    // so the list is neither copyable nor movable.
    Skiplist(const Skiplist &) = delete;
    Skiplist &operator=(const Skiplist &) = delete;
    Skiplist(Skiplist &&) = delete;
    Skiplist &operator=(Skiplist &&) = delete;

    void insert(const Key &key);
    bool contains(const Key &key) const;

    class Iterator {
      public:
        explicit Iterator(const Skiplist *list)
            : list_(list),
              node_(list_->head_->forward_[0].load(std::memory_order_acquire)) {
        }

        ~Iterator() = default;

        bool valid() const { return node_ != nullptr; }

        const Key &key() const {
            assert(valid());
            return node_->key_;
        }

        void next() {
            assert(valid());
            node_ = node_->forward_[0].load(std::memory_order_acquire);
        }

        void prev() {
            assert(valid());
            Node *p = node_->prev_.load(std::memory_order_acquire);
            node_ = (p == list_->head_) ? nullptr : p;
        }

        void seek(const Key &target) {
            node_ = list_->find_greater_or_equal_(target);
        }

        void seek_to_first() {
            node_ = list_->head_->forward_[0].load(std::memory_order_acquire);
        }

        void seek_to_last() {
            node_ = list_->find_last_();
            if (node_ == list_->head_) { node_ = nullptr; }
        }

      private:
        const Skiplist *list_;
        Node *node_;
    };

  private:
    Arena *arena_;
    Cmp cmp_;

    static constexpr int k_max_height_ = 12;

    std::atomic<int> curr_height_;
    Node *head_;

    Random rnd_;

    int random_height_();
    void clear_();
    Node *new_node_(const Key &key, int level);

    Node *find_greater_or_equal_(const Key &key) const;
    Node *find_last_() const;

    bool less_(const Key &a, const Key &b) const { return cmp_(a, b) < 0; }
    bool equal_(const Key &a, const Key &b) const { return cmp_(a, b) == 0; }
};

template <typename Key, class Comparator>
int Skiplist<Key, Comparator>::random_height_() {
    // Increase height with probability 1 in k_branching
    static const unsigned int k_branching = 4;
    int height = 1;
    while (height < k_max_height_ && rnd_.one_in(k_branching)) { height++; }
    assert(height > 0);
    assert(height <= k_max_height_);
    return height;
}

template <typename Key, class Comparator>
void Skiplist<Key, Comparator>::clear_() {
    if (!head_) return;
    // Destroy nodes; their storage is owned by the arena, not freed here.
    Node *curr = head_->forward_[0].load(std::memory_order_relaxed);
    while (curr != nullptr) {
        Node *next = curr->forward_[0].load(std::memory_order_relaxed);
        curr->~Node();
        curr = next;
    }
    for (int i = 0; i <= k_max_height_; ++i) {
        head_->forward_[i].store(nullptr, std::memory_order_relaxed);
    }
    curr_height_.store(0, std::memory_order_relaxed);
}

template <typename Key, class Comparator>
typename Skiplist<Key, Comparator>::Node *
Skiplist<Key, Comparator>::new_node_(const Key &key, int level) {
    size_t mem_size = sizeof(Node) + sizeof(std::atomic<Node *>) * level;
    char *node_mem = arena_->allocate_aligned(mem_size);
    return new (node_mem) Node(key, level);
}

template <typename Key, class Comparator>
typename Skiplist<Key, Comparator>::Node *
Skiplist<Key, Comparator>::find_greater_or_equal_(const Key &key) const {
    Node *curr = head_;
    int max_lvl = curr_height_.load(std::memory_order_acquire);

    for (int i = max_lvl; i >= 0; --i) {
        Node *next = curr->forward_[i].load(std::memory_order_acquire);
        while (next != nullptr && less_(next->key_, key)) {
            curr = next;
            next = curr->forward_[i].load(std::memory_order_acquire);
        }
    }
    return curr->forward_[0].load(std::memory_order_acquire);
}

template <typename Key, class Comparator>
typename Skiplist<Key, Comparator>::Node *
Skiplist<Key, Comparator>::find_last_() const {
    Node *curr = head_;
    int max_lvl = curr_height_.load(std::memory_order_acquire);

    for (int i = max_lvl; i >= 0; --i) {
        Node *next = curr->forward_[i].load(std::memory_order_acquire);
        while (next != nullptr) {
            curr = next;
            next = curr->forward_[i].load(std::memory_order_acquire);
        }
    }
    return curr;
}

template <typename Key, class Comparator>
void Skiplist<Key, Comparator>::insert(const Key &key) {
    Node *update[k_max_height_ + 1];
    Node *curr = head_;
    int max_lvl = curr_height_.load(std::memory_order_relaxed);

    for (int i = max_lvl; i >= 0; --i) {
        Node *next = curr->forward_[i].load(std::memory_order_relaxed);
        while (next != nullptr && less_(next->key_, key)) {
            curr = next;
            next = curr->forward_[i].load(std::memory_order_relaxed);
        }
        update[i] = curr;
    }

    curr = curr->forward_[0].load(std::memory_order_relaxed);
    if (curr != nullptr && equal_(curr->key_, key)) {
        return; // Key already present
    }

    int new_level = random_height_();
    if (new_level > max_lvl) {
        for (int i = max_lvl + 1; i <= new_level; ++i) { update[i] = head_; }
        curr_height_.store(new_level, std::memory_order_release);
    }

    Node *x = new_node_(key, new_level);
    Node *next_node = update[0]->forward_[0].load(std::memory_order_relaxed);

    x->prev_.store(update[0], std::memory_order_relaxed);

    for (int i = 0; i <= new_level; ++i) {
        x->forward_[i].store(
            update[i]->forward_[i].load(std::memory_order_relaxed),
            std::memory_order_relaxed);
    }

    // Publish x into each level, then fix the old successor's back pointer.
    for (int i = 0; i <= new_level; ++i) {
        update[i]->forward_[i].store(x, std::memory_order_release);
    }

    if (next_node != nullptr) {
        next_node->prev_.store(x, std::memory_order_release);
    }
}

template <typename Key, class Comparator>
bool Skiplist<Key, Comparator>::contains(const Key &key) const {
    Node *x = find_greater_or_equal_(key);
    return x != nullptr && equal_(key, x->key_);
}

} // namespace ANCHORDB_NAMESPACE
