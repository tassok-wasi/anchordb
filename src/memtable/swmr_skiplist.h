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

#ifdef SWMR_MEMTABLE_SKIPLIST
#include "arena.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <functional>
#include <random>
#endif

namespace anchordb {

#ifdef SWMR_MEMTABLE_SKIPLIST

template <typename Key, typename Value, typename Comparator = std::less<Key>>
class Skiplist {
  private:
    struct Node {
        Key const key_;
        Value const value_;
        int const level_;
        std::atomic<Node *> forward_[1];

        Node(const Key &k, const Value &v, int lvl)
            : key_(k), value_(v), level_(lvl) {
            for (int i = 0; i <= lvl; ++i) {
                forward_[i].store(nullptr, std::memory_order_relaxed);
            }
        }

        Node(const Key &k, Value &&v, int lvl)
            : key_(k), value_(std::move(v)), level_(lvl) {
            for (int i = 0; i <= lvl; ++i) {
                forward_[i].store(nullptr, std::memory_order_relaxed);
            }
        }

        ~Node() = default;
    };

  public:
    explicit Skiplist(Comparator cmp, Arena *arena)
        : cmp_(cmp), arena_(arena), gen_(std::random_device{}()),
          dist_(prob_factor_), curr_level_{0} {
        head_ = new_node_(Key{}, Value{}, max_level_);
        for (int i = 0; i <= max_level_; ++i) {
            head_->forward_[i].store(nullptr, std::memory_order_relaxed);
        }
    }

    ~Skiplist() { clear_(); }

    Skiplist(const Skiplist &) = delete;
    Skiplist &operator=(const Skiplist &) = delete;

    Skiplist(Skiplist &&other) noexcept
        : cmp_(std::move(other.cmp_)), arena_(std::move(other.arena_)),
          gen_(std::move(other.gen_)), dist_(std::move(other.dist_)),
          curr_level_{other.curr_level_.load()} {
        other.head_ = nullptr;
        other.curr_level_.store(0);
    }

    Skiplist &operator=(Skiplist &&other) noexcept {
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

    void insert(const Key &key, const Value &value);
    void insert(const Key &key, Value &&value);

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

        const Value &value() const {
            assert(valid());
            return node_->value_;
        }

        void next() {
            assert(valid());
            node_ = node_->forward_[0].load(std::memory_order_acquire);
        }

        void prev() {
            assert(valid());
            if (node_ == list_->head_) {
                node_ = nullptr;
                return;
            }
            Node *curr = list_->head_;
            int max_lvl = list_->curr_level_.load(std::memory_order_acquire);
            for (int i = max_lvl; i >= 0; --i) {
                Node *next = curr->forward_[i].load(std::memory_order_acquire);
                while (next != nullptr &&
                       list_->less_(next->key_, node_->key_)) {
                    curr = next;
                    next = curr->forward_[i].load(std::memory_order_acquire);
                }
            }
            node_ = (curr == list_->head_) ? nullptr : curr;
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

    std::atomic<int> curr_level_;
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

// default copy overload
template <typename Key, typename Value, typename Comparator>
typename Skiplist<Key, Value, Comparator>::Node *
Skiplist<Key, Value, Comparator>::new_node_(const Key &key, const Value &value,
                                            int level) {
    size_t mem_size = sizeof(Node) + sizeof(std::atomic<Node *>) * level;
    char *node_mem = arena_->allocate_aligned(mem_size);
    return new (node_mem) Node(key, value, level);
}

// move overload
template <typename Key, typename Value, typename Comparator>
typename Skiplist<Key, Value, Comparator>::Node *
Skiplist<Key, Value, Comparator>::new_node_(const Key &key, Value &&value,
                                            int level) {
    size_t mem_size = sizeof(Node) + sizeof(std::atomic<Node *>) * level;
    char *node_mem = arena_->allocate_aligned(mem_size);
    return new (node_mem) Node(key, std::move(value), level);
}

template <typename Key, typename Value, typename Comparator>
typename Skiplist<Key, Value, Comparator>::Node *
Skiplist<Key, Value, Comparator>::find_greater_or_equal_(const Key &key) const {
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

template <typename Key, typename Value, typename Comparator>
typename Skiplist<Key, Value, Comparator>::Node *
Skiplist<Key, Value, Comparator>::find_last_() const {
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

template <typename Key, typename Value, typename Comparator>
void Skiplist<Key, Value, Comparator>::insert(const Key &key,
                                              const Value &value) {
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

    Node *x = new_node_(key, value, new_level);

    for (int i = 0; i <= new_level; ++i) {
        x->forward_[i].store(
            update[i]->forward_[i].load(std::memory_order_relaxed),
            std::memory_order_relaxed);
    }

    for (int i = 0; i <= new_level; ++i) {
        update[i]->forward_[i].store(x, std::memory_order_release);
    }
}

template <typename Key, typename Value, typename Comparator>
void Skiplist<Key, Value, Comparator>::insert(const Key &key, Value &&value) {
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

    Node *x = new_node_(key, std::move(value), new_level);

    for (int i = 0; i <= new_level; ++i) {
        x->forward_[i].store(
            update[i]->forward_[i].load(std::memory_order_relaxed),
            std::memory_order_relaxed);
    }

    for (int i = 0; i <= new_level; ++i) {
        update[i]->forward_[i].store(x, std::memory_order_release);
    }
}

template <typename Key, typename Value, typename Comparator>
bool Skiplist<Key, Value, Comparator>::contains(const Key &key) const {
    Node *x = find_greater_or_equal_(key);
    return x != nullptr && equal_(key, x->key_);
}

#endif

} // namespace anchordb
