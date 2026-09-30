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

#include "anchordb/hash_map.h"
#include "anchordb/slice.h"
namespace anchordb {

class List {
  public:
    explicit List(Map *index_map);

    ~List() { clear(); }

    List(const List &other);
    List &operator=(const List &other);

    List(List &&other) noexcept;
    List &operator=(List &&other) noexcept;

    [[nodiscard]] bool is_empty() const { return head_ == nullptr; }
    [[nodiscard]] size_t size() const { return size_; }

    [[nodiscard]] bool contains(const HashedKey &key) const {
        Slice val = Slice();
        bool ok = index_map_->find(key, val);

        Node *node = nullptr;
        std::memcpy(&node, val.data(), sizeof(Node *));
        return ok && node != nullptr;
    }

    [[nodiscard]] Slice get(const HashedKey &key) const;

    void push_front(const HashedKey &key, const Slice &data);
    void push_back(const HashedKey &key, const Slice &data);

    void insert_before(const HashedKey &tr_key, const HashedKey &new_key,
                       const Slice &data);
    void insert_after(const HashedKey &tr_key, const HashedKey &new_key,
                      const Slice &data);

    void move_to_front(const HashedKey &key);
    void move_to_back(const HashedKey &key);
    void pop_front();
    void pop_back();
    void remove(const HashedKey &key);
    void clear();

  private:
    struct Node {
        Slice key;
        Slice data;
        Node *prev;
        Node *next;

        Node(const Slice &key, const Slice &data)
            : key(key), data(data), prev(nullptr), next(nullptr) {}

        ~Node() {
            prev = nullptr;
            next = nullptr;
        }
    };

    Node *head_{nullptr};
    Node *tail_{nullptr};
    Map *index_map_;
    size_t size_{0};

    void link_empty_(Node *node);
    void link_before_(Node *target, Node *node);
    void link_after_(Node *target, Node *node);
    void unlink_(Node *node);
    void remove_(Node *node);
};

} // namespace anchordb
