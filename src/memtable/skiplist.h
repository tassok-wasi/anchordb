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

#include "arena.h"
#include <functional>
#include <memory>
#include <random>
namespace anchordb {

template <typename Key, typename Value, typename Comparator = std::less<Key>>
class SkipList {

  public:
    SkipList(Comparator cmp, Arena *arena)
        : cmp_(cmp), arena_(arena), gen_(std::random_device{}()),
          dist_(prob_factor_) {}

    virtual ~SkipList() = default;

    SkipList(const SkipList<Key, Value, Comparator> &other) = delete;
    SkipList &operator=(const SkipList<Key, Value, Comparator> &other) = delete;

    SkipList(SkipList<Key, Value, Comparator> &&other)
        : cmp_(std::move(other.cmp_)), arena_(std::move(other.arena_)),
          gen_(std::move(other.gen_)), dist_(std::move(other.dist_)) {
        other.arena_ = nullptr;
        other.gen_ = std::mt19937{};
        other.dist_ = std::bernoulli_distribution{};
    }

    SkipList &operator=(SkipList<Key, Value, Comparator> &&other) {
        if (this != &other) {
            clear();

            cmp_ = std::move(other.cmp_);
            arena_ = std::move(other.arena_);
            gen_ = std::move(other.gen_);
            dist_ = std::move(other.dist_);
            other.arena_ = nullptr;
            other.gen_ = std::mt19937{};
            other.dist_ = std::bernoulli_distribution{};
        }
        return *this;
    }

    virtual void insert(const Key &key, const Value &value) = 0;
    virtual void insert(const Key &key, Value &&value) = 0;
    virtual bool contains(const Key &key) = 0;

    class Iterator {
      public:
        virtual ~Iterator() = default;

        virtual bool valid() const = 0;
        virtual const Key &key() const = 0;
        virtual const Value &value() const = 0;

        virtual void next() = 0;
        virtual void prev() = 0;
        virtual void seek() = 0;
        virtual void seek_to_frist() = 0;
        virtual void seek_to_last() = 0;
    };

    virtual std::unique_ptr<Iterator> new_iter() = 0;

  protected:
    virtual int random_level() = 0;
    virtual void clear() = 0;

    virtual bool less(const Key &a, const Key &b) const { return cmp_(a, b); }
    virtual bool equal(const Key &a, const Key &b) const {
        return !cmp_(a, b) && !cmp_(b, a);
    }

    int max_level_ = 12;
    float prob_factor_ = 0.5f;

    Arena *arena_;
    Comparator cmp_;

    std::mt19937 gen_;
    std::bernoulli_distribution dist_;
};
} // namespace anchordb
