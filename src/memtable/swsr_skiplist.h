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
#include <cassert>
#include <cstddef>
#include <random>
#include <string_view>

namespace anchordb {

template <typename Key, class Comparator> class Skiplist_SWSR {
  private:
    struct Node {
        const Key key_;

        Node *prev_;
        Node *forward_[1];

        Node(const Key &k, int lvl) : key_(k), prev_(nullptr) {}

        ~Node() = default;
    };

  public:
    explicit Skiplist_SWSR(Comparator cmp, Arena *arena)
        : cmp_(cmp), arena_(arena), gen_(std::random_device{}()),
          dist_(prob_factor_), curr_level_{0} {
        head_ = new_node_(0, max_level_);
        for (int i = 0; i <= max_level_; ++i) {
            head_->forward_[i] = nullptr;
        }
    }

    ~Skiplist_SWSR() { clear_(); }

    Skiplist_SWSR(const Skiplist_SWSR &other) = delete;
    Skiplist_SWSR &operator=(const Skiplist_SWSR &other) = delete;

    Skiplist_SWSR(Skiplist_SWSR &&other) noexcept
        : cmp_(std::move(other.cmp_)), arena_(std::move(other.arena_)),
          gen_(std::move(other.gen_)), dist_(std::move(other.dist_)),
          curr_level_{other.curr_level_}, head_{other.head_} {
        other.head_ = nullptr;
        other.curr_level_ = 0;
        other.arena_ = nullptr;
    }

    Skiplist_SWSR &operator=(Skiplist_SWSR &&other) noexcept {
        if (this != &other) {
            clear_();

            cmp_ = std::move(other.cmp_);
            arena_ = std::move(other.arena_);
            gen_ = std::move(other.gen_);
            dist_ = std::move(other.dist_);
            curr_level_ = other.curr_level_;
            head_ = other.head_;
            other.head_ = nullptr;
            other.curr_level_ = 0;
        }
        return *this;
    }

    void insert(const Key &key);
    bool contains(const Key &key) const;

    class Iterator {
      public:
        explicit Iterator(const Skiplist_SWSR *list)
            : list_(list), node_(list->head_->forward_[0]) {}

        ~Iterator() = default;

        bool valid() const { return node_ != nullptr; }

        const Key &key() const {
            assert(valid());
            return node_->key_;
        }

        const std::string_view &value() const {
            assert(valid());
            return node_->value_;
        }

        void next() {
            assert(valid());
            node_ = node_->forward_[0];
        }

        void prev() {
            assert(valid());
            node_ = (node_->prev_ == list_->head_) ? nullptr : node_->prev_;
        }

        void seek(const Key &target) {
            node_ = list_->find_greater_or_equal_(target);
        }

        void seek_to_first() { node_ = list_->head_->forward_[0]; }

        void seek_to_last() {
            node_ = list_->find_last_();
            if (node_ == list_->head_) {
                node_ = nullptr;
            }
        }

      private:
        const Skiplist_SWSR *list_;
        Node *node_;
    };

  private:
    Arena *arena_;
    Comparator cmp_;

    int max_level_ = 12;
    float prob_factor_ = 0.5f;

    int curr_level_;
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
int Skiplist_SWSR<Key, Comparator>::random_level_() {
    int lvl = 0;
    while (dist_(gen_) && lvl < max_level_) {
        lvl++;
    }
    return lvl;
}

template <typename Key, class Comparator>
void Skiplist_SWSR<Key, Comparator>::clear_() {
    if (!head_)
        return;
    Node *curr = head_->forward_[0];
    while (curr != nullptr) {
        Node *next = curr->forward_[0];
        curr->~Node();
        curr = next;
    }
    for (int i = 0; i <= max_level_; ++i) {
        head_->forward_[i] = nullptr;
    }
    curr_level_ = 0;
}

template <typename Key, class Comparator>
typename Skiplist_SWSR<Key, Comparator>::Node *
Skiplist_SWSR<Key, Comparator>::new_node_(const Key &key, int level) {
    size_t mem_size = sizeof(Node) + sizeof(Node *) * level;
    char *node_mem = arena_->allocate_aligned(mem_size);
    return new (node_mem) Node(key, level);
}

template <typename Key, class Comparator>
typename Skiplist_SWSR<Key, Comparator>::Node *
Skiplist_SWSR<Key, Comparator>::find_greater_or_equal_(const Key &key) const {
    Node *curr = head_;
    for (int i = curr_level_; i >= 0; --i) {
        while (curr->forward_[i] != nullptr &&
               less_(curr->forward_[i]->key_, key)) {
            curr = curr->forward_[i];
        }
    }
    return curr->forward_[0];
}

template <typename Key, class Comparator>
typename Skiplist_SWSR<Key, Comparator>::Node *
Skiplist_SWSR<Key, Comparator>::find_last_() const {
    Node *curr = head_;
    for (int i = curr_level_; i >= 0; --i) {
        while (curr->forward_[i] != nullptr) {
            curr = curr->forward_[i];
        }
    }
    return curr;
}

template <typename Key, class Comparator>
void Skiplist_SWSR<Key, Comparator>::insert(const Key &key) {
    Node *update[max_level_ + 1];
    Node *curr = head_;
    for (int i = curr_level_; i >= 0; --i) {
        while (curr->forward_[i] != nullptr &&
               less_(curr->forward_[i]->key_, key)) {
            curr = curr->forward_[i];
        }
        update[i] = curr;
    }

    curr = curr->forward_[0];
    if (curr != nullptr && equal_(curr->key_, key)) {
        return; // Key already exists
    }

    int new_level = random_level_();
    if (new_level > curr_level_) {
        for (int i = curr_level_ + 1; i <= new_level; ++i) {
            update[i] = head_;
        }
        curr_level_ = new_level;
    }

    Node *x = new_node_(key, new_level);
    Node *next_node = update[0]->forward_[0];

    x->prev_ = update[0];

    for (int i = 0; i <= new_level; ++i) {
        x->forward_[i] = update[i]->forward_[i];
        update[i]->forward_[i] = x;
    }

    if (next_node != nullptr) {
        next_node->prev_ = x;
    }
}

template <typename Key, class Comparator>
bool Skiplist_SWSR<Key, Comparator>::contains(const Key &key) const {
    Node *x = find_greater_or_equal_(key);
    return x != nullptr && equal_(key, x->key_);
}

} // namespace anchordb
