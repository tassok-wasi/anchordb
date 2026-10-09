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

#include "clock_pro.h"

namespace anchordb {

Cache::Handle *ClockProCache::insert(const Slice &key, uint64_t hash,
                                     void *value, size_t charge,
                                     void (*deleter)(const Slice &key,
                                                     void *value)) {
    mutex_.lock();

    ClockProHandle *e = std::visit(
        [&](auto &active_map) { return active_map.get(HashedKey{key, hash}); },
        map_);

    if (e != nullptr) {
        if (e->is_resident) {
            // Overwrite existing resident entry payload
            if (e->deleter && e->value) {
                (*e->deleter)(e->key(), e->value);
            }
            usage_ -= e->charge;
            if (e->is_hot) {
                hot_usage_ -= e->charge;
            }
        } else {
            // Non-resident history hit -> Promote to HOT entry
            if (cold_capacity_ > e->charge) {
                cold_capacity_ -= e->charge;
            } else {
                cold_capacity_ = 0;
            }
            e->is_hot = true;
        }

        e->value = value;
        e->deleter = deleter;
        e->charge = charge;
        e->is_resident = true;
        e->ref_bit = true;
        usage_ += charge;

        if (e->is_hot) {
            hot_usage_ += charge;
        }

        ref(e);
        evict();
        mutex_.unlock();
        return reinterpret_cast<Cache::Handle *>(e);
    }

    // Allocate new node
    e = reinterpret_cast<ClockProHandle *>(
        malloc(sizeof(ClockProHandle) - 1 + key.size()));

    e->value = value;
    e->deleter = deleter;
    e->charge = charge;
    e->key_len = key.size();
    e->hash = hash;
    e->in_cache = true;
    e->is_resident = true;
    e->is_hot = false;
    e->ref_bit = false;
    e->refs = 2; // 1 for returned handle, 1 for cache internal structure
    std::memcpy(e->key_data, key.data(), key.size());

    usage_ += charge;
    ring_add(e);

    std::visit(
        [&](auto &active_map) {
            active_map.insert(HashedKey{e->key(), e->hash}, e);
        },
        map_);

    evict();
    mutex_.unlock();
    return reinterpret_cast<Cache::Handle *>(e);
}

Cache::Handle *ClockProCache::lookup(const Slice &key, uint64_t hash) {
    mutex_.lock();
    ClockProHandle *e = std::visit(
        [&](auto &active_map) { return active_map.get(HashedKey{key, hash}); },
        map_);

    if (e != nullptr && e->is_resident) {
        e->ref_bit = true;
        ref(e);
    } else {
        e = nullptr; // History nodes return null on lookup
    }

    mutex_.unlock();
    return reinterpret_cast<Cache::Handle *>(e);
}

void ClockProCache::release(Cache::Handle *handle) {
    mutex_.lock();
    unref(reinterpret_cast<ClockProHandle *>(handle));
    mutex_.unlock();
}

void ClockProCache::erase(const Slice &key, uint64_t hash) {
    mutex_.lock();
    ClockProHandle *e = std::visit(
        [&](auto &active_map) {
            return active_map.remove(HashedKey{key, hash});
        },
        map_);
    finish_erase(e);
    mutex_.unlock();
}

void ClockProCache::prune() {
    mutex_.lock();
    while (!ring_.is_empty()) {
        ClockProHandle *e = ring_.head();
        std::visit(
            [&](auto &active_map) {
                active_map.remove(HashedKey{e->key(), e->hash});
            },
            map_);
        finish_erase(e);
    }
    mutex_.unlock();
}

void ClockProCache::evict() {
    while (usage_ > capacity_ && !ring_.is_empty()) {
        hand_cold_step();
    }
}

void ClockProCache::hand_cold_step() {
    if (ring_.is_empty())
        return;

    ClockProHandle *e = hand_cold_;
    if (e == nullptr) {
        e = ring_.head();
        hand_cold_ = e;
    }

    if (!e->is_hot) {
        if (e->ref_bit) {
            // Second chance granted -> Promote to HOT
            e->is_hot = true;
            e->ref_bit = false;
            hot_usage_ += e->charge;

            // Balance hot/cold ratio if hot capacity exceeded
            while (hot_usage_ > (capacity_ - cold_capacity_)) {
                hand_hot_step();
            }
        } else {
            // Evict cold entry payload
            if (e->is_resident) {
                if (e->value && e->deleter) {
                    (*e->deleter)(e->key(), e->value);
                    e->value = nullptr;
                }
                e->is_resident = false;
                usage_ -= e->charge;
            } else {
                // Remove non-resident history node completely. ring_remove()
                // advances hand_cold_ past the freed node.
                finish_erase(e);
                return;
            }
        }
    }

    hand_cold_ = ring_.step(e);
}

void ClockProCache::hand_hot_step() {
    if (ring_.is_empty())
        return;

    ClockProHandle *e = hand_hot_;
    if (e == nullptr) {
        e = ring_.head();
        hand_hot_ = e;
    }

    if (e->is_hot) {
        if (e->ref_bit) {
            e->ref_bit = false;
        } else {
            // Demote HOT entry to COLD
            e->is_hot = false;
            hot_usage_ -= e->charge;
        }
    }

    hand_hot_ = ring_.step(e);
}

void ClockProCache::hand_test_step() {
    if (ring_.is_empty())
        return;

    ClockProHandle *e = hand_test_;
    if (e == nullptr) {
        e = ring_.head();
        hand_test_ = e;
    }

    // Test hand reclaims non-resident COLD history nodes that were never
    // re-referenced. ring_remove() advances hand_test_ past the freed node.
    if (!e->is_hot && !e->is_resident) {
        finish_erase(e);
        return;
    }

    hand_test_ = ring_.step(e);
}

void ClockProCache::ring_add(ClockProHandle *e) {
    ring_.push_back(HashedKey{e->key(), e->hash}, e);
}

void ClockProCache::ring_remove(ClockProHandle *e) {
    // Advance any hand that points at the node being unlinked so it never
    // dangles after the node is freed.
    ClockProHandle *next = ring_.step(e);
    if (next == e) {
        next = nullptr; // e was the only node in the ring
    }
    if (hand_cold_ == e) {
        hand_cold_ = next;
    }
    if (hand_hot_ == e) {
        hand_hot_ = next;
    }
    if (hand_test_ == e) {
        hand_test_ = next;
    }

    ring_.remove(e);
}

void ClockProCache::ref(ClockProHandle *e) { e->refs++; }

void ClockProCache::unref(ClockProHandle *e) {
    assert(e->refs > 0);
    e->refs--;
    if (e->refs == 0) {
        assert(!e->in_cache);
        if (e->is_resident && e->deleter && e->value) {
            (*e->deleter)(e->key(), e->value);
        }
        free(e);
    }
}

bool ClockProCache::finish_erase(ClockProHandle *e) {
    if (e != nullptr) {
        if (e->is_resident) {
            usage_ -= e->charge;
            if (e->is_hot) {
                hot_usage_ -= e->charge;
            }
        }
        ring_remove(e);
        e->in_cache = false;
        unref(e);
    }
    return e != nullptr;
}

Cache *new_clock_pro_cache(size_t capacity, MapType t) {
    return new ShardedClockProCache(capacity, t);
}

} // namespace anchordb
