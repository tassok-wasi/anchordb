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

template <typename Key, typename Comparator> class Skiplist_MWMR {
  private:
    struct Node {
        const Key key_;
        const int level_;
        const Slice value_;

        std::atomic<Node *> prev_;
        std::atomic<Node *> forward_[1];

        Node(const Key &k, const Slice &v, int lvl)
            : key_(k), level_(lvl), value_(v) {
            for (int i = 0; i <= lvl; ++i) {
                forward_[i].store(nullptr, std::memory_order_relaxed);
            }
        }

        ~Node() = default;
    };

  public:
    explicit Skiplist_MWMR(Comparator cmp, Arena *arena)
        : cmp_(cmp), arena_(arena), curr_level_{0},
          gen_(std::random_device{}()), dist_(prob_factor_) {
        head_ = new_node_(Key{}, Slice(), max_level_);
        for (int i = 0; i <= max_level_; ++i) {
            head_->forward_[i].store(nullptr, std::memory_order_relaxed);
        }
    }

    ~Skiplist_MWMR() { clear_(); }

    Skiplist_MWMR(const Skiplist_MWMR &other) = delete;
    Skiplist_MWMR &operator=(const Skiplist_MWMR &other) = delete;

    Skiplist_MWMR(Skiplist_MWMR &&other) noexcept
        : cmp_(std::move(other.cmp_)), arena_(std::move(other.arena_)),
          gen_(std::move(other.gen_)), dist_(std::move(other.dist_)),
          curr_level_{other.curr_level_.load()} {
        other.head_ = nullptr;
        other.curr_level_.store(0);
        other.arena_ = nullptr;
    }

    Skiplist_MWMR &operator=(Skiplist_MWMR &&other) noexcept {
        if (this != &other) {
            clear_();

            cmp_ = std::move(other.cmp_);
            arena_ = std::move(other.arena_);
            gen_ = std::move(other.gen_);
            dist_ = std::move(other.dist_);
            curr_level_.store(other.curr_level_.load());
            other.head_ = nullptr;
            other.curr_level_.store(0);
            other.arena_ = nullptr;
            other.gen_ = std::mt19937{};
            other.dist_ = std::bernoulli_distribution{};
        }
        return *this;
    }

    void insert(const Key &key, const Slice &value);
    bool contains(const Key &key) const;

    class Iterator {
      public:
        explicit Iterator(const Skiplist_MWMR *list)
            : list_(list),
              node_(list->head_->forward_[0].load(std::memory_order_acquire)) {}

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
        const Skiplist_MWMR *list_;
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
    Node *new_node_(const Key &key, const Slice &value, int level);

    bool find_splice_pointers_(const Key &key, Node **update,
                               Node **next_nodes) const;
    Node *find_greater_or_equal_(const Key &key) const;
    Node *find_last_() const;

    bool less_(const Key &a, const Key &b) const { return cmp_(a, b) < 0; }
    bool equal_(const Key &a, const Key &b) const { return cmp_(a, b) == 0; }
    bool greater_(const Key &a, const Key &b) const { return cmp_(a, b) > 0; }
};

template <typename Key, typename Comparator>
int Skiplist_MWMR<Key, Comparator>::random_level_() {
    int lvl = 0;
    while (dist_(gen_) && lvl < max_level_) {
        lvl++;
    }
    return lvl;
}

template <typename Key, typename Comparator>
void Skiplist_MWMR<Key, Comparator>::clear_() {
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

template <typename Key, typename Comparator>
typename Skiplist_MWMR<Key, Comparator>::Node *
Skiplist_MWMR<Key, Comparator>::new_node_(const Key &key, const Slice &value,
                                          int level) {
    size_t mem_size = sizeof(Node) + sizeof(std::atomic<Node *>) * level;
    char *node_mem = arena_->allocate_aligned(mem_size);
    return new (node_mem) Node(key, value, level);
}

template <typename Key, typename Comparator>
bool Skiplist_MWMR<Key, Comparator>::find_splice_pointers_(
    const Key &key, Node **update, Node **next_nodes) const {

    Node *curr = head_;
    int max_lvl = curr_level_.load(std::memory_order_acquire);

    for (int i = max_lvl; i >= 0; --i) {
        Node *next = curr->forward_[i].load(std::memory_order_acquire);
        while (next != nullptr && less_(next->key_, key)) {
            curr = next;
            next = curr->forward_[i].load(std::memory_order_acquire);
        }
        update[i] = curr;
        next_nodes[i] = next;
    }

    return (next_nodes[0] != nullptr && equal_(next_nodes[0]->key_, key));
}

template <typename Key, typename Comparator>
typename Skiplist_MWMR<Key, Comparator>::Node *
Skiplist_MWMR<Key, Comparator>::find_greater_or_equal_(const Key &key) const {
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

template <typename Key, typename Comparator>
typename Skiplist_MWMR<Key, Comparator>::Node *
Skiplist_MWMR<Key, Comparator>::find_last_() const {
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

template <typename Key, typename Comparator>
void Skiplist_MWMR<Key, Comparator>::insert(const Key &key,
                                            const Slice &value) {
    Node *update[max_level_ + 1];
    Node *next_nodes[max_level_ + 1];

    Node *x = nullptr;
    int new_level = -1;

    // Level 0 Logical Insertion (CAS loop)
    while (true) {
        if (find_splice_pointers_(key, update, next_nodes)) {
            // Key already present in the list
            if (x != nullptr) {
                x->~Node(); // Clean up unused allocated memory
            }
            return;
        }

        if (x == nullptr) {
            new_level = random_level_();
            x = new_node_(key, value, new_level);
        }

        x->prev_.store(update[0], std::memory_order_relaxed);

        // Initialize forward pointers of x to expected next nodes
        for (int i = 0; i <= new_level; ++i) {
            x->forward_[i].store(next_nodes[i], std::memory_order_relaxed);
        }

        // Attempt CAS link at Level 0
        Node *expected_next = next_nodes[0];
        if (update[0]->forward_[0].compare_exchange_weak(
                expected_next, x, std::memory_order_release,
                std::memory_order_relaxed)) {

            if (expected_next != nullptr) {
                expected_next->prev_.store(x, std::memory_order_release);
            }
            break;
        }

        // CAS failed due to concurrent modification on Level 0; retry
    }

    // Update overall max level using CAS if necessary
    int current_max = curr_level_.load(std::memory_order_relaxed);
    while (new_level > current_max) {
        if (curr_level_.compare_exchange_weak(current_max, new_level,
                                              std::memory_order_release,
                                              std::memory_order_relaxed)) {
            break;
        }
    }

    // Level 1..new_level Physical Splicing (CAS loops)
    for (int i = 1; i <= new_level; ++i) {
        while (true) {
            Node *pred = update[i];
            Node *expected_next = next_nodes[i];

            x->forward_[i].store(expected_next, std::memory_order_relaxed);

            if (pred->forward_[i].compare_exchange_weak(
                    expected_next, x, std::memory_order_release,
                    std::memory_order_relaxed)) {
                break; // Upper level link set successfully
            }

            // Retry search to update splice pointers for remaining higher
            // levels
            find_splice_pointers_(key, update, next_nodes);
        }
    }
}

template <typename Key, typename Comparator>
bool Skiplist_MWMR<Key, Comparator>::contains(const Key &key) const {
    Node *x = find_greater_or_equal_(key);
    return x != nullptr && equal_(key, x->key_);
}

} // namespace anchordb
