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

#include "lirs.h"

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

Cache::Handle *LIRSCache::insert(std::string_view key, uint64_t hash,
                                 void *value, size_t charge,
                                 void (*deleter)(std::string_view key,
                                                 void *value)) {
    mutex_.lock();

    // Check if key already exists in cache/history
    LIRSHandle *e = std::visit(
        [&](auto &active_map) { return active_map.get(HashedKey{key, hash}); },
        map_);

    if (e != nullptr) {
        if (e->is_resident) {
            // Overwrite existing resident node
            if (e->deleter && e->value) {
                (*e->deleter)(e->key(), e->value);
            }
            usage_ -= e->charge;
            if (e->is_lir) {
                lir_usage_ -= e->charge;
            }
        }

        e->value = value;
        e->deleter = deleter;
        e->charge = charge;
        e->is_resident = true;
        usage_ += charge;
        if (e->is_lir) {
            lir_usage_ += charge;
        }

        // Access/Promotion logic
        if (e->is_lir) {
            bool was_bottom = (!stack_.is_empty() && stack_.back() == e);
            stack_.remove(HashedKey{e->key(), e->hash});
            stack_.push_front(HashedKey{e->key(), e->hash}, e);
            if (was_bottom) {
                prune_stack();
            }
        } else { // HIR block
            if (e->in_stack) {
                // Promoted to LIR
                e->is_lir = true;
                lir_usage_ += e->charge;
                stack_.remove(HashedKey{e->key(), e->hash});
                stack_.push_front(HashedKey{e->key(), e->hash}, e);

                if (e->in_hir_list) {
                    hir_list_.remove(HashedKey{e->key(), e->hash});
                    e->in_hir_list = false;
                }

                // Demote bottom-most LIR block to HIR to maintain balance
                if (lir_usage_ > lir_capacity_ && !stack_.is_empty()) {
                    LIRSHandle *bottom_lir = stack_.back();
                    assert(bottom_lir->is_lir);
                    bottom_lir->is_lir = false;
                    lir_usage_ -= bottom_lir->charge;

                    stack_.remove(
                        HashedKey{bottom_lir->key(), bottom_lir->hash});
                    bottom_lir->in_stack = false;

                    hir_list_.push_front(
                        HashedKey{bottom_lir->key(), bottom_lir->hash},
                        bottom_lir);
                    bottom_lir->in_hir_list = true;

                    prune_stack();
                }
            } else {
                // Remain HIR, move to top of stack and Q list
                stack_.push_front(HashedKey{e->key(), e->hash}, e);
                e->in_stack = true;

                if (e->in_hir_list) {
                    hir_list_.remove(HashedKey{e->key(), e->hash});
                }
                hir_list_.push_front(HashedKey{e->key(), e->hash}, e);
                e->in_hir_list = true;
            }
        }

        ref(e); // Reference count for caller
        evict();
        mutex_.unlock();
        return reinterpret_cast<Cache::Handle *>(e);
    }

    // New key insertion
    e = reinterpret_cast<LIRSHandle *>(
        malloc(sizeof(LIRSHandle) - 1 + key.size()));

    e->value = value;
    e->deleter = deleter;
    e->charge = charge;
    e->key_len = key.size();
    e->hash = hash;
    e->in_cache = true;
    e->is_resident = true;
    e->refs = 2; // 1 for caller, 1 for cache internal structure
    e->in_stack = false;
    e->in_hir_list = false;
    std::memcpy(e->key_data, key.data(), key.size());

    if (lir_usage_ < lir_capacity_) {
        // Cold start: Insert directly as LIR block
        e->is_lir = true;
        lir_usage_ += charge;
        usage_ += charge;
        stack_.push_front(HashedKey{e->key(), e->hash}, e);
        e->in_stack = true;
    } else {
        // Cache full/warmed up: Insert as HIR block
        e->is_lir = false;
        usage_ += charge;
        stack_.push_front(HashedKey{e->key(), e->hash}, e);
        e->in_stack = true;

        hir_list_.push_front(HashedKey{e->key(), e->hash}, e);
        e->in_hir_list = true;
    }

    std::visit(
        [&](auto &active_map) {
            active_map.insert(HashedKey{e->key(), e->hash}, e);
        },
        map_);

    evict();
    mutex_.unlock();
    return reinterpret_cast<Cache::Handle *>(e);
}

Cache::Handle *LIRSCache::lookup(std::string_view key, uint64_t hash) {
    mutex_.lock();
    LIRSHandle *e = std::visit(
        [&](auto &active_map) { return active_map.get(HashedKey{key, hash}); },
        map_);

    if (e != nullptr && e->is_resident) {
        ref(e);

        if (e->is_lir) {
            bool was_bottom = (!stack_.is_empty() && stack_.back() == e);
            stack_.remove(HashedKey{e->key(), e->hash});
            stack_.push_front(HashedKey{e->key(), e->hash}, e);
            if (was_bottom) {
                prune_stack();
            }
        } else { // HIR block hit
            if (e->in_stack) {
                // Promote HIR block to LIR block
                e->is_lir = true;
                lir_usage_ += e->charge;

                stack_.remove(HashedKey{e->key(), e->hash});
                stack_.push_front(HashedKey{e->key(), e->hash}, e);

                if (e->in_hir_list) {
                    hir_list_.remove(HashedKey{e->key(), e->hash});
                    e->in_hir_list = false;
                }

                // Demote bottom-most LIR block to HIR
                if (lir_usage_ > lir_capacity_ && !stack_.is_empty()) {
                    LIRSHandle *bottom_lir = stack_.back();
                    assert(bottom_lir->is_lir);
                    bottom_lir->is_lir = false;
                    lir_usage_ -= bottom_lir->charge;

                    stack_.remove(
                        HashedKey{bottom_lir->key(), bottom_lir->hash});
                    bottom_lir->in_stack = false;

                    hir_list_.push_front(
                        HashedKey{bottom_lir->key(), bottom_lir->hash},
                        bottom_lir);
                    bottom_lir->in_hir_list = true;

                    prune_stack();
                }
            } else {
                // Non-stack HIR hit: Push to top of S and top of Q
                stack_.push_front(HashedKey{e->key(), e->hash}, e);
                e->in_stack = true;

                if (e->in_hir_list) {
                    hir_list_.remove(HashedKey{e->key(), e->hash});
                }
                hir_list_.push_front(HashedKey{e->key(), e->hash}, e);
                e->in_hir_list = true;
            }
        }
    } else {
        e = nullptr; // Non-resident history hits return null on lookup
    }

    mutex_.unlock();
    return reinterpret_cast<Cache::Handle *>(e);
}

void LIRSCache::release(Cache::Handle *handle) {
    mutex_.lock();
    unref(reinterpret_cast<LIRSHandle *>(handle));
    mutex_.unlock();
}

void LIRSCache::erase(std::string_view key, uint64_t hash) {
    mutex_.lock();
    LIRSHandle *e = std::visit(
        [&](auto &active_map) {
            return active_map.remove(HashedKey{key, hash});
        },
        map_);
    finish_erase(e);
    mutex_.unlock();
}

void LIRSCache::prune() {
    mutex_.lock();
    while (!hir_list_.is_empty()) {
        LIRSHandle *e = hir_list_.back();
        std::visit(
            [&](auto &active_map) {
                active_map.remove(HashedKey{e->key(), e->hash});
            },
            map_);
        finish_erase(e);
    }
    mutex_.unlock();
}

void LIRSCache::clear_all() {
    // Every tracked entry lives in the map and is referenced from stack_ and/or
    // hir_list_; draining both lists (finish_erase unlinks from both) frees
    // them all.
    while (!stack_.is_empty() || !hir_list_.is_empty()) {
        LIRSHandle *e = !stack_.is_empty() ? stack_.back() : hir_list_.back();
        std::visit(
            [&](auto &active_map) {
                active_map.remove(HashedKey{e->key(), e->hash});
            },
            map_);
        finish_erase(e);
    }
}

void LIRSCache::prune_stack() {
    while (!stack_.is_empty()) {
        LIRSHandle *bottom = stack_.back();
        if (bottom->is_lir) {
            break; // Stop at the first LIR block
        }

        // Remove non-LIR items at the bottom of the stack
        stack_.pop_back();
        bottom->in_stack = false;

        // If it's a non-resident HIR entry no longer in stack, free its history
        // handle
        if (!bottom->is_resident && !bottom->in_hir_list) {
            std::visit(
                [&](auto &active_map) {
                    active_map.remove(HashedKey{bottom->key(), bottom->hash});
                },
                map_);
            bottom->in_cache = false;
            unref(bottom);
        }
    }
}

void LIRSCache::evict() {
    while (usage_ > capacity_ && !hir_list_.is_empty()) {
        LIRSHandle *old = hir_list_.back();
        hir_list_.pop_back();
        old->in_hir_list = false;

        // Evict value payload
        if (old->value && old->deleter) {
            (*old->deleter)(old->key(), old->value);
            old->value = nullptr;
        }

        old->is_resident = false;
        usage_ -= old->charge;

        // If no longer tracked in stack, clear history record completely
        if (!old->in_stack) {
            std::visit(
                [&](auto &active_map) {
                    active_map.remove(HashedKey{old->key(), old->hash});
                },
                map_);
            old->in_cache = false;
            unref(old);
        }
    }
}

void LIRSCache::ref(LIRSHandle *e) { e->refs++; }

void LIRSCache::unref(LIRSHandle *e) {
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

bool LIRSCache::finish_erase(LIRSHandle *e) {
    if (e != nullptr) {
        if (e->in_stack) {
            stack_.remove(HashedKey{e->key(), e->hash});
            e->in_stack = false;
        }
        if (e->in_hir_list) {
            hir_list_.remove(HashedKey{e->key(), e->hash});
            e->in_hir_list = false;
        }
        if (e->is_resident) {
            usage_ -= e->charge;
            if (e->is_lir) {
                lir_usage_ -= e->charge;
            }
        }
        e->in_cache = false;
        prune_stack();
        unref(e);
    }
    return e != nullptr;
}

Cache *new_lirs_cache(size_t capacity, MapType t) {
    return new ShardedLIRSCache(capacity, t);
}

} // namespace anchordb
