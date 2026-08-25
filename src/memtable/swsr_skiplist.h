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
#include <anchordb/macro.h>

#ifdef SWSR_MEMTABLE_SKIPLIST
#include "arena.h"
#include <algorithm>
#include <cassert>
#include <functional>
#include <random>
#endif

namespace anchordb {

#ifdef SWSR_MEMTABLE_SKIPLIST

template <typename Key, typename Value, typename Comparator = std::less<Key>>
class Skiplist {
  private:
    struct Node {
        Key const key_;
        Value const value_;
        int const level_;
        Node *forward_[1];

        Node(const Key &k, const Value &v, int lvl)
            : key_(k), value_(v), level_(lvl) {}

        Node(const Key &k, Value &&v, int lvl)
            : key_(k), value_(std::move(v)), level_(lvl) {}

        ~Node() = default;
    };

  public:
    explicit Skiplist(Comparator cmp, Arena *arena)
        : cmp_(cmp), arena_(arena), gen_(std::random_device{}()),
          dist_(prob_factor_), curr_level_{0} {
        head_ = new_node_(Key{}, Value{}, max_level_);
        for (int i = 0; i <= max_level_; ++i) {
            head_->forward_[i] = nullptr;
        }
    }

    ~Skiplist() { clear_(); }

    Skiplist(const Skiplist &other) = delete;
    Skiplist &operator=(const Skiplist &other) = delete;

    Skiplist(Skiplist &&other) noexcept
        : cmp_(std::move(other.cmp_)), arena_(std::move(other.arena_)),
          gen_(std::move(other.gen_)), dist_(std::move(other.dist_)),
          curr_level_{other.curr_level_}, head_{other.head_} {
        other.head_ = nullptr;
        other.curr_level_ = 0;
        other.arena_ = nullptr;
    }

    Skiplist &operator=(Skiplist &&other) noexcept {
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

    void insert(const Key &key, const Value &value);
    void insert(const Key &key, Value &&value);
    bool contains(const Key &key) const;

    class Iterator {
      public:
        explicit Iterator(const Skiplist *list)
            : list_(list), node_(list->head_->forward_[0]) {}

        ~Iterator() = default;

        bool valid() const { return node_ != nullptr; }

        const Key &key() const {
            assert(valid());
            return node_->key_;
        }

        const Value &value() const {
            assert(valid());
            return node_->value_;
        }

        void next() {
            assert(valid());
            node_ = node_->forward_[0];
        }

        void prev() {
            assert(valid());
            if (node_ == list_->head_) {
                node_ = nullptr;
                return;
            }
            Node *curr = list_->head_;
            for (int i = list_->curr_level_; i >= 0; --i) {
                while (curr->forward_[i] != nullptr &&
                       list_->less_(curr->forward_[i]->key, node_->key_)) {
                    curr = curr->forward_[i];
                }
            }
            node_ = (curr == list_->head_) ? nullptr : curr;
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
        const Skiplist *list_;
        Node *node_;
    };

  private:
    int random_level_();
    void clear_();
    Node *new_node_(const Key &key, const Value &value, int level);
    Node *new_node_(const Key &key, Value &&value, int level);

    Node *find_greater_or_equal_(const Key &key) const;
    Node *find_last_() const;

    bool less_(const Key &a, const Key &b) const { return cmp_(a, b); }
    bool equal_(const Key &a, const Key &b) const {
        return !cmp_(a, b) && !cmp_(b, a);
    }

    int max_level_ = 12;
    float prob_factor_ = 0.5f;

    int curr_level_;
    Node *head_;

    Arena *arena_;
    Comparator cmp_;

    std::mt19937 gen_;
    std::bernoulli_distribution dist_;
};

template <typename Key, typename Value, typename Comparator>
int Skiplist<Key, Value, Comparator>::random_level_() {
    int lvl = 0;
    while (dist_(gen_) && lvl < max_level_) {
        lvl++;
    }
    return lvl;
}

template <typename Key, typename Value, typename Comparator>
void Skiplist<Key, Value, Comparator>::clear_() {
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

// default copy overload
template <typename Key, typename Value, typename Comparator>
typename Skiplist<Key, Value, Comparator>::Node *
Skiplist<Key, Value, Comparator>::new_node_(const Key &key, const Value &value,
                                            int level) {
    size_t mem_size = sizeof(Node) + sizeof(Node *) * level;
    char *node_mem = arena_->allocate_aligned(mem_size);
    return new (node_mem) Node(key, value, level);
}

// Move overload
template <typename Key, typename Value, typename Comparator>
typename Skiplist<Key, Value, Comparator>::Node *
Skiplist<Key, Value, Comparator>::new_node_(const Key &key, Value &&value,
                                            int level) {
    size_t mem_size = sizeof(Node) + sizeof(Node *) * level;
    char *node_mem = arena_->allocate_aligned(mem_size);
    return new (node_mem) Node(key, std::move(value), level);
}

template <typename Key, typename Value, typename Comparator>
typename Skiplist<Key, Value, Comparator>::Node *
Skiplist<Key, Value, Comparator>::find_greater_or_equal_(const Key &key) const {
    Node *curr = head_;
    for (int i = curr_level_; i >= 0; --i) {
        while (curr->forward_[i] != nullptr &&
               less_(curr->forward_[i]->key, key)) {
            curr = curr->forward_[i];
        }
    }
    return curr->forward_[0];
}

template <typename Key, typename Value, typename Comparator>
typename Skiplist<Key, Value, Comparator>::Node *
Skiplist<Key, Value, Comparator>::find_last_() const {
    Node *curr = head_;
    for (int i = curr_level_; i >= 0; --i) {
        while (curr->forward_[i] != nullptr) {
            curr = curr->forward_[i];
        }
    }
    return curr;
}

// default copy overload
template <typename Key, typename Value, typename Comparator>
void Skiplist<Key, Value, Comparator>::insert(const Key &key,
                                              const Value &value) {
    Node *update[max_level_ + 1];
    Node *curr = head_;
    for (int i = curr_level_; i >= 0; --i) {
        while (curr->forward_[i] != nullptr &&
               less_(curr->forward_[i]->key, key)) {
            curr = curr->forward_[i];
        }
        update[i] = curr;
    }

    curr = curr->forward_[0];
    if (curr != nullptr && equal(curr->key_, key)) {
        return; // Key already exists
    }

    int new_level = random_level_();
    if (new_level > curr_level_) {
        for (int i = curr_level_ + 1; i <= new_level; ++i) {
            update[i] = head_;
        }
        curr_level_ = new_level;
    }

    Node *x = new_node_(key, value, new_level);
    for (int i = 0; i <= new_level; ++i) {
        x->forward_[i] = update[i]->forward_[i];
        update[i]->forward_[i] = x;
    }
}

// Move overload
template <typename Key, typename Value, typename Comparator>
void Skiplist<Key, Value, Comparator>::insert(const Key &key, Value &&value) {
    Node *update[max_level_ + 1];
    Node *curr = head_;
    for (int i = curr_level_; i >= 0; --i) {
        while (curr->forward_[i] != nullptr &&
               less(curr->forward_[i]->key, key)) {
            curr = curr->forward_[i];
        }
        update[i] = curr;
    }

    curr = curr->forward_[0];
    if (curr != nullptr && equal(curr->key_, key)) {
        return; // Key already exists
    }

    int new_level = random_level_();
    if (new_level > curr_level_) {
        for (int i = curr_level_ + 1; i <= new_level; ++i) {
            update[i] = head_;
        }
        curr_level_ = new_level;
    }

    Node *x = new_node_(key, std::move(value), new_level);
    for (int i = 0; i <= new_level; ++i) {
        x->forward_[i] = update[i]->forward_[i];
        update[i]->forward_[i] = x;
    }
}

template <typename Key, typename Value, typename Comparator>
bool Skiplist<Key, Value, Comparator>::contains(const Key &key) const {
    Node *x = find_greater_or_equal_(key);
    return x != nullptr && equal(key, x->key_);
}

#endif

} // namespace anchordb
