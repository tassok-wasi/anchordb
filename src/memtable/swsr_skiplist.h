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
#include <cassert>
#include <functional>
#include <memory>
#include <new>
#include <random>

namespace anchordb {

template <typename Key, typename Value, typename Comparator = std::less<Key>>
class SWSR_SkipList : public SkipList<Key, Value, Comparator> {
  private:
    struct Node {
        Key const key;
        Value const value;
        int const level;
        Node *forward[1]; // Flexible array member pattern

        Node(const Key &k, const Value &v, int lvl)
            : key(k), value(v), level(lvl) {}

        Node(const Key &k, Value &&v, int lvl)
            : key(k), value(std::move(v)), level(lvl) {}
    };

  public:
    using Base = SkipList<Key, Value, Comparator>;
    explicit SWSR_SkipList(Comparator cmp, Arena *arena)
        : Base::Base(cmp, arena), curr_level_{0} {
        head_ = new_node(Key{}, Value{}, Base::max_level_);
        for (int i = 0; i <= Base::max_level_; ++i) {
            head_->forward[i] = nullptr;
        }
    }

    ~SWSR_SkipList() override { clear(); }

    SWSR_SkipList(SWSR_SkipList &&other) noexcept
        : Base::Base(std::move(other)), curr_level_{other.curr_level_},
          head_{other.head_} {
        other.head_ = nullptr;
        other.curr_level_ = 0;
        other.arena_ = nullptr;
    }

    SWSR_SkipList &operator=(SWSR_SkipList &&other) noexcept {
        if (this != &other) {
            Base::operator=(std::move(other));
            curr_level_ = other.curr_level_;
            head_ = other.head_;
            other.head_ = nullptr;
            other.curr_level_ = 0;
        }
        return *this;
    }

    void insert(const Key &key, const Value &value) override;
    void insert(const Key &key, Value &&value) override;
    bool contains(const Key &key) const override;

    class Iterator : public Base::Iterator {
      public:
        explicit Iterator(const SWSR_SkipList *list)
            : list_(list), node_(list->head_->forward[0]) {}

        ~Iterator() override {} // TODO: what to do here?

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
            node_ = node_->forward[0];
        }

        void prev() override {
            assert(valid());
            if (node_ == list_->head_) {
                node_ = nullptr;
                return;
            }
            Node *curr = list_->head_;
            for (int i = list_->curr_level_; i >= 0; --i) {
                while (curr->forward[i] != nullptr &&
                       list_->less(curr->forward[i]->key, node_->key)) {
                    curr = curr->forward[i];
                }
            }
            node_ = (curr == list_->head_) ? nullptr : curr;
        }

        void seek(const Key &target) override {
            node_ = list_->find_greater_or_equal(target);
        }

        void seek_to_first() override { node_ = list_->head_->forward[0]; }

        void seek_to_last() override {
            node_ = list_->find_last();
            if (node_ == list_->head_) {
                node_ = nullptr;
            }
        }

      private:
        const SWSR_SkipList *list_;
        Node *node_;
    };

    std::unique_ptr<typename Base::Iterator> new_iter() override {
        return std::make_unique<Iterator>(this);
    }

  private:
    int random_level() override;
    void clear() override;
    Node *new_node(const Key &key, const Value &value, int level);
    Node *new_node(const Key &key, Value &&value, int level);

    Node *find_greater_or_equal(const Key &key) const;
    Node *find_last() const;

    int curr_level_;
    Node *head_;
};

template <typename Key, typename Value, typename Comparator>
int SWSR_SkipList<Key, Value, Comparator>::random_level() {
    int lvl = 0;
    while (dist_(Base::gen_) && lvl < Base::max_level_) {
        lvl++;
    }
    return lvl;
}

template <typename Key, typename Value, typename Comparator>
void SWSR_SkipList<Key, Value, Comparator>::clear() {
    if (!head_)
        return;
    Node *curr = head_->forward[0];
    while (curr != nullptr) {
        Node *next = curr->forward[0];
        curr->~Node();
        curr = next;
    }
    for (int i = 0; i <= Base::max_level_; ++i) {
        head_->forward[i] = nullptr;
    }
    curr_level_ = 0;
}

// default copy overload
template <typename Key, typename Value, typename Comparator>
typename SWSR_SkipList<Key, Value, Comparator>::Node *
SWSR_SkipList<Key, Value, Comparator>::new_node(const Key &key,
                                                const Value &value, int level) {
    size_t mem_size = sizeof(Node) + sizeof(Node *) * level;
    char *node_mem = Base::arena_->allocate_aligned(mem_size);
    return new (node_mem) Node(key, value, level);
}

// Move overload
template <typename Key, typename Value, typename Comparator>
typename SWSR_SkipList<Key, Value, Comparator>::Node *
SWSR_SkipList<Key, Value, Comparator>::new_node(const Key &key, Value &&value,
                                                int level) {
    size_t mem_size = sizeof(Node) + sizeof(Node *) * level;
    char *node_mem = Base::arena_->allocate_aligned(mem_size);
    return new (node_mem) Node(key, std::move(value), level);
}

template <typename Key, typename Value, typename Comparator>
typename SWSR_SkipList<Key, Value, Comparator>::Node *
SWSR_SkipList<Key, Value, Comparator>::find_greater_or_equal(
    const Key &key) const {
    Node *curr = head_;
    for (int i = curr_level_; i >= 0; --i) {
        while (curr->forward[i] != nullptr &&
               Base::less(curr->forward[i]->key, key)) {
            curr = curr->forward[i];
        }
    }
    return curr->forward[0];
}

template <typename Key, typename Value, typename Comparator>
typename SWSR_SkipList<Key, Value, Comparator>::Node *
SWSR_SkipList<Key, Value, Comparator>::find_last() const {
    Node *curr = head_;
    for (int i = curr_level_; i >= 0; --i) {
        while (curr->forward[i] != nullptr) {
            curr = curr->forward[i];
        }
    }
    return curr;
}

// default copy overload
template <typename Key, typename Value, typename Comparator>
void SWSR_SkipList<Key, Value, Comparator>::insert(const Key &key,
                                                   const Value &value) {
    Node *update[Base::max_level_ + 1];
    Node *curr = head_;
    for (int i = curr_level_; i >= 0; --i) {
        while (curr->forward[i] != nullptr &&
               Base::less(curr->forward[i]->key, key)) {
            curr = curr->forward[i];
        }
        update[i] = curr;
    }

    curr = curr->forward[0];
    if (curr != nullptr && Base::equal(curr->key, key)) {
        return; // Key already exists
    }

    int new_level = random_level();
    if (new_level > curr_level_) {
        for (int i = curr_level_ + 1; i <= new_level; ++i) {
            update[i] = head_;
        }
        curr_level_ = new_level;
    }

    Node *x = new_node(key, value, new_level);
    for (int i = 0; i <= new_level; ++i) {
        x->forward[i] = update[i]->forward[i];
        update[i]->forward[i] = x;
    }
}

// Move overload
template <typename Key, typename Value, typename Comparator>
void SWSR_SkipList<Key, Value, Comparator>::insert(const Key &key,
                                                   Value &&value) {
    Node *update[Base::max_level_ + 1];
    Node *curr = head_;
    for (int i = curr_level_; i >= 0; --i) {
        while (curr->forward[i] != nullptr &&
               Base::less(curr->forward[i]->key, key)) {
            curr = curr->forward[i];
        }
        update[i] = curr;
    }

    curr = curr->forward[0];
    if (curr != nullptr && Base::equal(curr->key, key)) {
        return; // Key already exists
    }

    int new_level = random_level();
    if (new_level > curr_level_) {
        for (int i = curr_level_ + 1; i <= new_level; ++i) {
            update[i] = head_;
        }
        curr_level_ = new_level;
    }

    Node *x = new_node(key, std::move(value), new_level);
    for (int i = 0; i <= new_level; ++i) {
        x->forward[i] = update[i]->forward[i];
        update[i]->forward[i] = x;
    }
}

template <typename Key, typename Value, typename Comparator>
bool SWSR_SkipList<Key, Value, Comparator>::contains(const Key &key) const {
    Node *x = find_greater_or_equal(key);
    return x != nullptr && Base::equal(key, x->key);
}

} // namespace anchordb
