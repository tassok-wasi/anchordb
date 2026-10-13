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

#include "lru.h"

namespace anchordb {

Cache::Handle *LRUCache::insert(std::string_view key, uint64_t hash,
                                void *value, size_t charge,
                                void (*deleter)(std::string_view key,
                                                void *value)) {
    mutex_.lock();
    LRUHandle *e = reinterpret_cast<LRUHandle *>(
        malloc(sizeof(LRUHandle) - 1 + key.size()));

    e->value = value;
    e->deleter = deleter;
    e->charge = charge;
    e->key_len = key.size();
    e->hash = hash;
    e->in_cache = false;
    e->refs = 1; // for the returned handle
    std::memcpy(e->key_data, key.data(), key.size());

    if (capacity_ > 0) {
        e->refs++; // for the cache's reference
        e->in_cache = true;
        // The returned handle keeps the entry in use until the caller releases
        // it, so it starts on the in-use list.
        in_use_.push_front(HashedKey{e->key(), e->hash}, e);
        usage_ += charge;

        // Replace any previous entry for this key.
        LRUHandle *old = std::visit(
            [&](auto &active_map) {
                return active_map.get(HashedKey{e->key(), e->hash});
            },
            map_);
        std::visit(
            [&](auto &active_map) {
                active_map.insert(HashedKey{e->key(), e->hash}, e);
            },
            map_);
        finish_erase(old);
    }

    while (usage_ > capacity_ && !lru_.is_empty()) {
        LRUHandle *old = lru_.back();
        assert(old->refs == 1);
        std::visit(
            [&](auto &active_map) {
                active_map.remove(HashedKey{old->key(), old->hash});
            },
            map_);
        bool erased = finish_erase(old);
        if (!erased) {
            assert(erased);
        }
    }

    mutex_.unlock();
    return reinterpret_cast<Cache::Handle *>(e);
}

Cache::Handle *LRUCache::lookup(std::string_view key, uint64_t hash) {
    mutex_.lock();
    LRUHandle *e = std::visit(
        [&](auto &active_map) { return active_map.get(HashedKey{key, hash}); },
        map_);
    if (e != nullptr) {
        ref(e);
    }
    mutex_.unlock();
    return reinterpret_cast<Cache::Handle *>(e);
}

void LRUCache::release(Cache::Handle *handle) {
    mutex_.lock();
    unref(reinterpret_cast<LRUHandle *>(handle));
    mutex_.unlock();
}

void LRUCache::erase(std::string_view key, uint64_t hash) {
    mutex_.lock();
    LRUHandle *e = std::visit(
        [&](auto &active_map) {
            return active_map.remove(HashedKey{key, hash});
        },
        map_);
    finish_erase(e);
    mutex_.unlock();
}

void LRUCache::prune() {
    mutex_.lock();
    while (!lru_.is_empty()) {
        LRUHandle *e = lru_.back();
        assert(e->refs == 1);
        std::visit(
            [&](auto &active_map) {
                active_map.remove(HashedKey{e->key(), e->hash});
            },
            map_);
        bool erased = finish_erase(e);
        if (!erased)
            assert(erased);
    }
    mutex_.unlock();
}

void LRUCache::ref(LRUHandle *e) {
    if (e->refs == 1 && e->in_cache) {
        lru_.remove(HashedKey{e->key(), e->hash});
        in_use_.push_front(HashedKey{e->key(), e->hash}, e);
    }
    e->refs++;
}

void LRUCache::unref(LRUHandle *e) {
    assert(e->refs > 0);
    e->refs--;
    if (e->refs == 0) {
        assert(!e->in_cache);
        (*e->deleter)(e->key(), e->value);
        free(e);
    } else if (e->in_cache && e->refs == 1) {
        in_use_.remove(HashedKey{e->key(), e->hash});
        lru_.push_front(HashedKey{e->key(), e->hash}, e);
    }
}

bool LRUCache::finish_erase(LRUHandle *e) {
    if (e != nullptr) {
        assert(e->in_cache);
        HashedKey k{e->key(), e->hash};
        if (lru_.contains(k)) {
            lru_.remove(k);
        } else {
            in_use_.remove(k);
        }
        e->in_cache = false;
        usage_ -= e->charge;
        unref(e);
    }
    return e != nullptr;
}

Cache *new_lru_cache(size_t capacity, MapType t) {
    return new ShardedLRUCache(capacity, t);
}

} // namespace anchordb
