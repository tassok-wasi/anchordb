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
#include "skiplist.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <functional>
#include <new>
#include <random>

namespace anchordb {

template <typename Key, typename Value, typename Comparator = std::less<Key>>
class SWMR_SkipList : public SkipList<Key, Value, Comparator> {
  private:
    struct Node {
        Key const key;
        Value const value;
        int const level;
        std::atomic<Node *> forward[1];

        Node(const Key &k, const Value &v, int lvl)
            : key(k), value(v), level(lvl) {
            for (int i = 0; i <= lvl; ++i) {
                forward[i].store(nullptr, std::memory_order_relaxed);
            }
        }

        Node(const Key &k, Value &&v, int lvl)
            : key(k), value(std::move(v)), level(lvl) {
            for (int i = 0; i <= lvl; ++i) {
                forward[i].store(nullptr, std::memory_order_relaxed);
            }
        }
    };

  public:
    using Base = SkipList<Key, Value, Comparator>;

    explicit SWMR_SkipList(Comparator cmp, Arena *arena)
        : Base(cmp, arena), curr_level_{0} {
        head_ = new_node(Key{}, Value{}, Base::max_level_);
    }

    ~SWMR_SkipList() override { clear(); }

    SWMR_SkipList(const SWMR_SkipList &) = delete;
    SWMR_SkipList &operator=(const SWMR_SkipList &) = delete;

    SWMR_SkipList(SWMR_SkipList &&other) noexcept
        : Base(std::move(other)), curr_level_{other.curr_level_.load()} {
        other.head_ = nullptr;
        other.curr_level_.store(0);
    }

    SWMR_SkipList &operator=(SWMR_SkipList &&other) noexcept {
        if (this != &other) {
            Base::operator=(std::move(other));
            curr_level_.store(other.curr_level_.load());
            other.head_ = nullptr;
            other.curr_level_.store(0);
        }
        return *this;
    }

    void insert(const Key &key, const Value &value) override;
    void insert(const Key &key, Value &&value) override;

    bool contains(const Key &key) const override;

    class Iterator : public Base::Iterator {
      public:
        explicit Iterator(const SWMR_SkipList *list)
            : list_(list),
              node_(list_->head_->forward[0].load(std::memory_order_acquire)) {}

        bool valid() const override { return node_ != nullptr; }

        const Key &key() const override {
            assert(valid());
            return node_->key;
        }

        const Value &value() const override {
            assert(valid());
            return node_->value;
        }

        void next() override {
            assert(valid());
            node_ = node_->forward[0].load(std::memory_order_acquire);
        }

        void prev() override {
            assert(valid());
            if (node_ == list_->head_) {
                node_ = nullptr;
                return;
            }
            Node *curr = list_->head_;
            int max_lvl = list_->curr_level_.load(std::memory_order_acquire);
            for (int i = max_lvl; i >= 0; --i) {
                Node *next = curr->forward[i].load(std::memory_order_acquire);
                while (next != nullptr && list_->less(next->key, node_->key)) {
                    curr = next;
                    next = curr->forward[i].load(std::memory_order_acquire);
                }
            }
            node_ = (curr == list_->head_) ? nullptr : curr;
        }

        void seek(const Key &target) override {
            node_ = list_->find_greater_or_equal(target);
        }

        void seek_to_first() override {
            node_ = list_->head_->forward[0].load(std::memory_order_acquire);
        }

        void seek_to_last() override {
            node_ = list_->find_last();
            if (node_ == list_->head_) {
                node_ = nullptr;
            }
        }

      private:
        const SWMR_SkipList *list_;
        Node *node_;
    };

  private:
    int random_level() override;
    void clear() override;
    Node *new_node(const Key &key, const Value &value, int level);
    Node *new_node(const Key &key, Value &&value, int level);

    Node *find_greater_or_equal(const Key &key) const;
    Node *find_last() const;

    std::atomic<int> curr_level_;
    Node *head_;
};

template <typename Key, typename Value, typename Comparator>
int SWMR_SkipList<Key, Value, Comparator>::random_level() {
    int lvl = 0;
    while (dist_(Base::gen_) && lvl < Base::max_level_) {
        lvl++;
    }
    return lvl;
}

template <typename Key, typename Value, typename Comparator>
void SWMR_SkipList<Key, Value, Comparator>::clear() {
    if (!head_)
        return;
    Node *curr = head_->forward[0].load(std::memory_order_relaxed);
    while (curr != nullptr) {
        Node *next = curr->forward[0].load(std::memory_order_relaxed);
        curr->~Node();
        curr = next;
    }
    for (int i = 0; i <= Base::max_level_; ++i) {
        head_->forward[i].store(nullptr, std::memory_order_relaxed);
    }
    curr_level_.store(0, std::memory_order_relaxed);
}

// default copy overload
template <typename Key, typename Value, typename Comparator>
typename SWMR_SkipList<Key, Value, Comparator>::Node *
SWMR_SkipList<Key, Value, Comparator>::new_node(const Key &key,
                                                const Value &value, int level) {
    size_t mem_size = sizeof(Node) + sizeof(std::atomic<Node *>) * level;
    char *node_mem = Base::arena_->allocate_aligned(mem_size);
    return new (node_mem) Node(key, value, level);
}

// move overload
template <typename Key, typename Value, typename Comparator>
typename SWMR_SkipList<Key, Value, Comparator>::Node *
SWMR_SkipList<Key, Value, Comparator>::new_node(const Key &key, Value &&value,
                                                int level) {
    size_t mem_size = sizeof(Node) + sizeof(std::atomic<Node *>) * level;
    char *node_mem = Base::arena_->allocate_aligned(mem_size);
    return new (node_mem) Node(key, std::move(value), level);
}

template <typename Key, typename Value, typename Comparator>
typename SWMR_SkipList<Key, Value, Comparator>::Node *
SWMR_SkipList<Key, Value, Comparator>::find_greater_or_equal(
    const Key &key) const {
    Node *curr = head_;
    int max_lvl = curr_level_.load(std::memory_order_acquire);

    for (int i = max_lvl; i >= 0; --i) {
        Node *next = curr->forward[i].load(std::memory_order_acquire);
        while (next != nullptr && Base::less(next->key, key)) {
            curr = next;
            next = curr->forward[i].load(std::memory_order_acquire);
        }
    }
    return curr->forward[0].load(std::memory_order_acquire);
}

template <typename Key, typename Value, typename Comparator>
typename SWMR_SkipList<Key, Value, Comparator>::Node *
SWMR_SkipList<Key, Value, Comparator>::find_last() const {
    Node *curr = head_;
    int max_lvl = curr_level_.load(std::memory_order_acquire);

    for (int i = max_lvl; i >= 0; --i) {
        Node *next = curr->forward[i].load(std::memory_order_acquire);
        while (next != nullptr) {
            curr = next;
            next = curr->forward[i].load(std::memory_order_acquire);
        }
    }
    return curr;
}

template <typename Key, typename Value, typename Comparator>
void SWMR_SkipList<Key, Value, Comparator>::insert(const Key &key,
                                                   const Value &value) {
    Node *update[Base::max_level_ + 1];
    Node *curr = head_;
    int max_lvl = curr_level_.load(std::memory_order_relaxed);

    for (int i = max_lvl; i >= 0; --i) {
        Node *next = curr->forward[i].load(std::memory_order_relaxed);
        while (next != nullptr && Base::less(next->key, key)) {
            curr = next;
            next = curr->forward[i].load(std::memory_order_relaxed);
        }
        update[i] = curr;
    }

    curr = curr->forward[0].load(std::memory_order_relaxed);
    if (curr != nullptr && Base::equal(curr->key, key)) {
        return; // Key already present
    }

    int new_level = random_level();
    if (new_level > max_lvl) {
        for (int i = max_lvl + 1; i <= new_level; ++i) {
            update[i] = head_;
        }
        curr_level_.store(new_level, std::memory_order_release);
    }

    Node *x = new_node(key, value, new_level);

    for (int i = 0; i <= new_level; ++i) {
        x->forward[i].store(
            update[i]->forward[i].load(std::memory_order_relaxed),
            std::memory_order_relaxed);
    }

    for (int i = 0; i <= new_level; ++i) {
        update[i]->forward[i].store(x, std::memory_order_release);
    }
}

template <typename Key, typename Value, typename Comparator>
void SWMR_SkipList<Key, Value, Comparator>::insert(const Key &key,
                                                   Value &&value) {
    Node *update[Base::max_level_ + 1];
    Node *curr = head_;
    int max_lvl = curr_level_.load(std::memory_order_relaxed);

    for (int i = max_lvl; i >= 0; --i) {
        Node *next = curr->forward[i].load(std::memory_order_relaxed);
        while (next != nullptr && Base::less(next->key, key)) {
            curr = next;
            next = curr->forward[i].load(std::memory_order_relaxed);
        }
        update[i] = curr;
    }

    curr = curr->forward[0].load(std::memory_order_relaxed);
    if (curr != nullptr && Base::equal(curr->key, key)) {
        return; // Key already present
    }

    int new_level = random_level();
    if (new_level > max_lvl) {
        for (int i = max_lvl + 1; i <= new_level; ++i) {
            update[i] = head_;
        }
        curr_level_.store(new_level, std::memory_order_release);
    }

    Node *x = new_node(key, std::move(value), new_level);

    for (int i = 0; i <= new_level; ++i) {
        x->forward[i].store(
            update[i]->forward[i].load(std::memory_order_relaxed),
            std::memory_order_relaxed);
    }

    for (int i = 0; i <= new_level; ++i) {
        update[i]->forward[i].store(x, std::memory_order_release);
    }
}

template <typename Key, typename Value, typename Comparator>
bool SWMR_SkipList<Key, Value, Comparator>::contains(const Key &key) const {
    Node *x = find_greater_or_equal(key);
    return x != nullptr && Base::equal(key, x->key);
}

} // namespace anchordb
