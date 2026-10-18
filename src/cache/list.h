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

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

template <typename T> class List {
  public:
    List() = default;

    ~List() { clear(); }

    List(const List &other);
    List &operator=(const List &other);

    List(List &&other) noexcept;
    List &operator=(List &&other) noexcept;

    bool is_empty() const { return head_ == nullptr; }
    size_t size() const { return size_; }

    bool contains(HashedKey key) const { return index_map_.contains(key); }

    T get(HashedKey key) const {
        Node *node = index_map_.get(key);
        if (node == nullptr) {
            return T();
        }

        return node->data;
    }

    void push_front(HashedKey key, T data);
    void push_back(HashedKey key, T data);

    void insert_before(HashedKey tr_key, HashedKey new_key, T data);
    void insert_after(HashedKey tr_key, HashedKey new_key, T data);

    void move_to_front(HashedKey key);
    void move_to_back(HashedKey key);

    void pop_front() {
        assert(head_ != nullptr);
        remove_(head_);
    }

    void pop_back() {
        assert(tail_ != nullptr);
        remove_(tail_);
    }

    void remove(HashedKey key) {
        Node *target_node = index_map_.get(key);
        assert(target_node != nullptr);

        remove_(target_node);
    }

    T front() { return head_->data; }
    T back() { return tail_->data; }

    void clear();

    // Public so that SwissMap<Node *> can be explicitly instantiated in
    // swiss_map.cc, where the map's template definitions live.
    struct Node {
        HashedKey hashed_key;
        T data;
        Node *prev;
        Node *next;

        Node(const HashedKey &hashed_key, T data)
            : hashed_key(hashed_key), data(data), prev(nullptr), next(nullptr) {
        }
    };

  private:
    Node *head_{nullptr};
    Node *tail_{nullptr};
    SwissMap<Node *> index_map_;
    size_t size_{0};

    void link_empty_(Node *node) {
        assert(node != nullptr);
        head_ = node;
        tail_ = node;
    }

    void link_before_(Node *target, Node *node);
    void link_after_(Node *target, Node *node);
    void unlink_(Node *node);

    void remove_(Node *node) {
        unlink_(node);
        index_map_.remove(node->hashed_key);
        delete node;
        size_--;
    }
};

template <typename T> List<T>::List(const List &other) : size_(0) {
    for (Node *n = other.head_; n != nullptr; n = n->next) {
        push_back(n->hashed_key, n->data);
    }
}

template <typename T> List<T> &List<T>::operator=(const List &other) {
    if (this != &other) {
        clear();
        for (Node *n = other.head_; n != nullptr; n = n->next) {
            push_back(n->hashed_key, n->data);
        }
    }
    return *this;
}

template <typename T>
List<T>::List(List &&other) noexcept
    : head_(other.head_), tail_(other.tail_),
      index_map_(std::move(other.index_map_)), size_(other.size_) {
    other.head_ = nullptr;
    other.tail_ = nullptr;
    other.size_ = 0;
}

template <typename T> List<T> &List<T>::operator=(List &&other) noexcept {
    if (this != &other) {
        clear();
        head_ = other.head_;
        tail_ = other.tail_;
        index_map_ = std::move(other.index_map_);
        size_ = other.size_;
        other.head_ = nullptr;
        other.tail_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

template <typename T> void List<T>::push_front(HashedKey key, T data) {
    if (contains(key))
        return;

    Node *new_node = new Node(key, data);
    if (is_empty()) {
        link_empty_(new_node);
    } else {
        link_before_(head_, new_node);
    }
    size_++;
    index_map_.insert(key, new_node);
}

template <typename T> void List<T>::push_back(HashedKey key, T data) {
    if (contains(key))
        return;

    Node *new_node = new Node(key, data);
    if (is_empty()) {
        link_empty_(new_node);
    } else {
        link_after_(tail_, new_node);
    }
    size_++;
    index_map_.insert(key, new_node);
}

template <typename T>
void List<T>::insert_before(HashedKey tr_key, HashedKey new_key, T data) {
    if (contains(new_key))
        return;

    Node *target_node = index_map_.get(tr_key);
    assert(target_node != nullptr);

    Node *new_node = new Node(new_key, data);
    link_before_(target_node, new_node);

    size_++;
    index_map_.insert(new_key, new_node);
}

template <typename T>
void List<T>::insert_after(HashedKey tr_key, HashedKey new_key, T data) {
    if (contains(new_key))
        return;

    Node *target_node = index_map_.get(tr_key);
    assert(target_node != nullptr);

    Node *new_node = new Node(new_key, data);
    link_after_(target_node, new_node);

    size_++;
    index_map_.insert(new_key, new_node);
}

template <typename T> void List<T>::move_to_front(HashedKey key) {
    Node *target_node = index_map_.get(key);
    assert(target_node != nullptr);

    if (target_node == head_)
        return;

    unlink_(target_node);
    link_before_(head_, target_node);
}

template <typename T> void List<T>::move_to_back(HashedKey key) {
    Node *target_node = index_map_.get(key);
    assert(target_node != nullptr);

    if (target_node == tail_)
        return;

    unlink_(target_node);
    link_after_(tail_, target_node);
}

template <typename T> void List<T>::clear() {
    while (head_ != nullptr) {
        Node *temp = head_;
        head_ = head_->next;
        delete temp;
    }
    tail_ = nullptr;
    size_ = 0;
    index_map_.clear();
}

template <typename T> void List<T>::link_before_(Node *target, Node *node) {
    assert(target != nullptr);
    assert(node != nullptr);

    node->next = target;
    node->prev = target->prev;

    if (target->prev != nullptr) {
        target->prev->next = node;
    } else {
        head_ = node;
    }
    target->prev = node;
}

template <typename T> void List<T>::link_after_(Node *target, Node *node) {
    assert(target != nullptr);
    assert(node != nullptr);

    node->next = target->next;
    node->prev = target;

    if (target->next != nullptr) {
        target->next->prev = node;
    } else {
        tail_ = node;
    }
    target->next = node;
}

template <typename T> void List<T>::unlink_(Node *node) {
    if (node == nullptr) {
        return;
    }
    if (node->prev != nullptr) {
        node->prev->next = node->next;
    }
    if (node->next != nullptr) {
        node->next->prev = node->prev;
    }
    if (node == head_) {
        head_ = node->next;
    }
    if (node == tail_) {
        tail_ = node->prev;
    }
    node->prev = nullptr;
    node->next = nullptr;
}

} // namespace anchordb
