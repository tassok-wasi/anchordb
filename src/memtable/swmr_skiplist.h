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
#include "arena.h"
#include <anchordb/comparator.h>
#include <anchordb/slice.h>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <random>

namespace anchordb {

template <typename Key, class Comparator> class Skiplist_SWMR {
  private:
    struct Node {
        const Key key_;

        std::atomic<Node *> prev_;
        std::atomic<Node *> forward_[1];

        Node(const Key &k, int lvl) : key_(k), prev_(nullptr) {}

        ~Node() = default;
    };

  public:
    explicit Skiplist_SWMR(Comparator cmp, Arena *arena)
        : cmp_(cmp), arena_(arena), gen_(std::random_device{}()),
          dist_(prob_factor_), curr_level_{0} {
        head_ = new_node_(0, max_level_);
        for (int i = 0; i <= max_level_; ++i) {
            head_->forward_[i].store(nullptr, std::memory_order_relaxed);
        }
    }

    ~Skiplist_SWMR() { clear_(); }

    Skiplist_SWMR(const Skiplist_SWMR &) = delete;
    Skiplist_SWMR &operator=(const Skiplist_SWMR &) = delete;

    Skiplist_SWMR(Skiplist_SWMR &&other) noexcept
        : cmp_(std::move(other.cmp_)), arena_(std::move(other.arena_)),
          gen_(std::move(other.gen_)), dist_(std::move(other.dist_)),
          curr_level_{other.curr_level_.load()} {
        other.head_ = nullptr;
        other.curr_level_.store(0);
    }

    Skiplist_SWMR &operator=(Skiplist_SWMR &&other) noexcept {
        if (this != &other) {
            clear_();

            cmp_ = std::move(other.cmp_);
            arena_ = std::move(other.arena_);
            gen_ = std::move(other.gen_);
            dist_ = std::move(other.dist_);
            curr_level_.store(other.curr_level_.load());
            other.head_ = nullptr;
            other.curr_level_.store(0);
        }
        return *this;
    }

    void insert(const Key &key);
    bool contains(const Key &key) const;

    class Iterator {
      public:
        explicit Iterator(const Skiplist_SWMR *list)
            : list_(list),
              node_(list_->head_->forward_[0].load(std::memory_order_acquire)) {
        }

        ~Iterator() = default;

        bool valid() const { return node_ != nullptr; }

        const Key &key() const {
            assert(valid());
            return node_->key_;
        }

        const Slice &value() const {
            assert(valid());
            return node_->value_;
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
            if (node_ == list_->head_) {
                node_ = nullptr;
            }
        }

      private:
        const Skiplist_SWMR *list_;
        Node *node_;
    };

  private:
    Arena *arena_;
    Comparator cmp_;

    int max_level_ = 12;
    float prob_factor_ = 0.5f;

    std::atomic<int> curr_level_;
    Node *head_;

    std::mt19937 gen_;
    std::bernoulli_distribution dist_;

    int random_level_();
    void clear_();
    Node *new_node_(const Key &key, int level);

    Node *find_greater_or_equal_(const Key &key) const;
    Node *find_last_() const;

    bool less_(const Key &a, const Key &b) const { return cmp_(a, b) < 0; }
    bool equal_(const Key &a, const Key &b) const { return cmp_(a, b) == 0; }
    bool greater_(const Key &a, const Key &b) const { return cmp_(a, b) > 0; }
};

template <typename Key, class Comparator>
int Skiplist_SWMR<Key, Comparator>::random_level_() {
    int lvl = 0;
    while (dist_(gen_) && lvl < max_level_) {
        lvl++;
    }
    return lvl;
}

template <typename Key, class Comparator>
void Skiplist_SWMR<Key, Comparator>::clear_() {
    if (!head_)
        return;
    Node *curr = head_->forward_[0].load(std::memory_order_relaxed);
    while (curr != nullptr) {
        Node *next = curr->forward_[0].load(std::memory_order_relaxed);
        curr->~Node();
        curr = next;
    }
    for (int i = 0; i <= max_level_; ++i) {
        head_->forward_[i].store(nullptr, std::memory_order_relaxed);
    }
    curr_level_.store(0, std::memory_order_relaxed);
}

template <typename Key, class Comparator>
typename Skiplist_SWMR<Key, Comparator>::Node *
Skiplist_SWMR<Key, Comparator>::new_node_(const Key &key, int level) {
    size_t mem_size = sizeof(Node) + sizeof(std::atomic<Node *>) * level;
    char *node_mem = arena_->allocate_aligned(mem_size);
    return new (node_mem) Node(key, level);
}

template <typename Key, class Comparator>
typename Skiplist_SWMR<Key, Comparator>::Node *
Skiplist_SWMR<Key, Comparator>::find_greater_or_equal_(const Key &key) const {
    Node *curr = head_;
    int max_lvl = curr_level_.load(std::memory_order_acquire);

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
typename Skiplist_SWMR<Key, Comparator>::Node *
Skiplist_SWMR<Key, Comparator>::find_last_() const {
    Node *curr = head_;
    int max_lvl = curr_level_.load(std::memory_order_acquire);

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
void Skiplist_SWMR<Key, Comparator>::insert(const Key &key) {
    Node *update[max_level_ + 1];
    Node *curr = head_;
    int max_lvl = curr_level_.load(std::memory_order_relaxed);

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

    int new_level = random_level_();
    if (new_level > max_lvl) {
        for (int i = max_lvl + 1; i <= new_level; ++i) {
            update[i] = head_;
        }
        curr_level_.store(new_level, std::memory_order_release);
    }

    Node *x = new_node_(key, new_level);
    Node *next_node = update[0]->forward_[0].load(std::memory_order_relaxed);

    x->prev_.store(update[0], std::memory_order_relaxed);

    for (int i = 0; i <= new_level; ++i) {
        x->forward_[i].store(
            update[i]->forward_[i].load(std::memory_order_relaxed),
            std::memory_order_relaxed);
    }

    for (int i = 0; i <= new_level; ++i) {
        update[i]->forward_[i].store(x, std::memory_order_release);
    }

    if (next_node != nullptr) {
        next_node->prev_.store(x, std::memory_order_release);
    }
}

template <typename Key, class Comparator>
bool Skiplist_SWMR<Key, Comparator>::contains(const Key &key) const {
    Node *x = find_greater_or_equal_(key);
    return x != nullptr && equal_(key, x->key_);
}

} // namespace anchordb
