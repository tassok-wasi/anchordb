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

#include "anchordb/slice.h"
#include "list.h"
#include <cassert>
#include <cstring>

namespace anchordb {

Slice List::get(const HashedKey &key) const {
    Slice val = Slice();
    bool ok = index_map_->find(key, val);
    if (!ok)
        return Slice();

    Node *node = nullptr;
    std::memcpy(&node, val.data(), sizeof(Node *));
    return node->data;
}

void List::push_front(const HashedKey &key, const Slice &data) {
    if (contains(key))
        return;

    Node *new_node = new Node(key.key, data);
    if (is_empty()) {
        link_empty_(new_node);
    } else {
        link_before_(head_, new_node);
    }
    size_++;
    index_map_->insert(
        key, Slice(reinterpret_cast<const char *>(&new_node), sizeof(Node *)));
}

void List::push_back(const HashedKey &key, const Slice &data) {
    if (contains(key))
        return;

    Node *new_node = new Node(key.key, data);
    if (is_empty()) {
        link_empty_(new_node);
    } else {
        link_after_(tail_, new_node);
    }
    size_++;
    index_map_->insert(
        key, Slice(reinterpret_cast<const char *>(&new_node), sizeof(Node *)));
}

void List::insert_before(const HashedKey &tr_key, const HashedKey &new_key,
                         const Slice &data) {
    if (contains(new_key))
        return;

    Slice target = Slice();
    bool ok = index_map_->find(tr_key, target);
    if (!ok)
        return;

    Node *target_node = nullptr;
    std::memcpy(&target_node, target.data(), sizeof(Node *));
    Node *new_node = new Node(new_key.key, data);

    link_before_(target_node, new_node);

    size_++;
    index_map_->insert(new_key, Slice(reinterpret_cast<const char *>(&new_node),
                                      sizeof(Node *)));
}
void List::insert_after(const HashedKey &tr_key, const HashedKey &new_key,
                        const Slice &data) {
    if (contains(new_key))
        return;

    Slice target = Slice();
    bool ok = index_map_->find(tr_key, target);
    if (!ok)
        return;

    Node *target_node = nullptr;
    std::memcpy(&target_node, target.data(), sizeof(Node *));
    Node *new_node = new Node(new_key.key, std::move(data));

    link_after_(target_node, new_node);

    size_++;
    index_map_->insert(new_key, Slice(reinterpret_cast<const char *>(&new_node),
                                      sizeof(Node *)));
}

void List::move_to_front(const HashedKey &key) {
    Slice target = Slice();
    bool ok = index_map_->find(key, target);
    if (!ok)
        return;

    Node *target_node = nullptr;
    std::memcpy(&target_node, target.data(), sizeof(Node *));

    if (target_node == head_)
        return;

    unlink_(target_node);
    link_before_(head_, target_node);
}

void List::move_to_back(const HashedKey &key) {
    Slice target = Slice();
    bool ok = index_map_->find(key, target);
    if (!ok)
        return;

    Node *target_node = nullptr;
    std::memcpy(&target_node, target.data(), sizeof(Node *));

    if (target_node == tail_)
        return;

    unlink_(target_node);
    link_after_(tail_, target_node);
}

void List::pop_front() {
    assert(head_ != nullptr);
    remove_(head_);
}

void List::pop_back() {
    assert(tail_ != nullptr);
    remove_(tail_);
}

void List::remove(const HashedKey &key) {
    Slice target = Slice();
    bool ok = index_map_->find(key, target);
    if (!ok)
        return;

    Node *target_node = nullptr;
    std::memcpy(&target_node, target.data(), sizeof(Node *));
    remove_(target_node);
}

void List::clear() {
    while (head_ != nullptr) {
        Node *temp = head_;
        head_ = head_->next;
        delete temp;
    }
    tail_ = nullptr;
    size_ = 0;
    index_map_->clear();
}

void List::link_empty_(Node *node) {
    assert(node != nullptr);
    head_ = node;
    tail_ = node;
}

void List::link_before_(Node *target, Node *node) {
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

void List::link_after_(Node *target, Node *node) {
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

void List::unlink_(Node *node) {
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
