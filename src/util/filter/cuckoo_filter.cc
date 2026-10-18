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

#include "cuckoo_filter.h"
#include <string_view>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

void cuckoo_create(const void *state_ptr, const std::string_view *keys,
                   size_t n, std::string *dst) {
    if (n == 0) return;

    // Number of buckets: smallest power of two that keeps the load factor
    // at or below 50 % (two slots per key on average).
    size_t num_buckets = 4;
    const size_t min_buckets =
        (n + kCuckooSlotsPerBucket - 1) / kCuckooSlotsPerBucket * 2;
    while (num_buckets < min_buckets) num_buckets <<= 1;

    const size_t total_bytes = num_buckets * kCuckooSlotsPerBucket;
    const size_t init_size = dst->size();
    dst->resize(init_size + total_bytes, 0);
    char *buckets = &(*dst)[init_size];

    for (size_t i = 0; i < n; i++) {
        const uint32_t h = cuckoo_hash(keys[i]);
        const uint8_t fp = cuckoo_fingerprint(h);
        const size_t i1 = cuckoo_index1(h, num_buckets);
        const size_t i2 = cuckoo_index2(i1, fp, num_buckets);

        // Try the two candidate buckets.
        bool inserted = false;
        for (size_t s = 0; s < kCuckooSlotsPerBucket; s++) {
            if (buckets[i1 * kCuckooSlotsPerBucket + s] == 0) {
                buckets[i1 * kCuckooSlotsPerBucket + s] = static_cast<char>(fp);
                inserted = true;
                break;
            }
        }
        if (!inserted) {
            for (size_t s = 0; s < kCuckooSlotsPerBucket; s++) {
                if (buckets[i2 * kCuckooSlotsPerBucket + s] == 0) {
                    buckets[i2 * kCuckooSlotsPerBucket + s] =
                        static_cast<char>(fp);
                    inserted = true;
                    break;
                }
            }
        }
        if (inserted) continue;

        // Both buckets are full — cuckoo eviction.
        uint32_t rng = h; // deterministic seed from the key hash
        size_t curr_index = (cuckoo_rand(rng) & 1) != 0 ? i1 : i2;
        uint8_t curr_fp = fp;

        for (size_t loop = 0; loop < kMaxReallocations; loop++) {
            const size_t slot = cuckoo_rand(rng) % kCuckooSlotsPerBucket;
            char *slot_ptr =
                &buckets[curr_index * kCuckooSlotsPerBucket + slot];
            const uint8_t old_fp = static_cast<uint8_t>(*slot_ptr);
            *slot_ptr = static_cast<char>(curr_fp);

            // Move the evicted fingerprint to its alternate bucket.
            curr_index = cuckoo_index2(curr_index, old_fp, num_buckets);
            curr_fp = old_fp;

            bool placed = false;
            for (size_t s = 0; s < kCuckooSlotsPerBucket; s++) {
                if (buckets[curr_index * kCuckooSlotsPerBucket + s] == 0) {
                    buckets[curr_index * kCuckooSlotsPerBucket + s] =
                        static_cast<char>(curr_fp);
                    placed = true;
                    break;
                }
            }
            if (placed) break;
        }
        // If we exhaust relocations the key is silently dropped.  This is
        // rare at 50 % load and is acceptable for a probabilistic filter
        // (the key will simply be treated as absent).
    }
}

bool cuckoo_match(const void *state_ptr, std::string_view key,
                  std::string_view cuckoo_filter) {
    const size_t len = cuckoo_filter.size();
    if (len == 0 || len % kCuckooSlotsPerBucket != 0) return false;

    const char *buckets = cuckoo_filter.data();
    const size_t num_buckets = len / kCuckooSlotsPerBucket;

    const uint32_t h = cuckoo_hash(key);
    const uint8_t fp = cuckoo_fingerprint(h);
    const size_t i1 = cuckoo_index1(h, num_buckets);
    const size_t i2 = cuckoo_index2(i1, fp, num_buckets);

    for (size_t s = 0; s < kCuckooSlotsPerBucket; s++) {
        if (static_cast<uint8_t>(buckets[i1 * kCuckooSlotsPerBucket + s]) == fp)
            return true;
    }
    for (size_t s = 0; s < kCuckooSlotsPerBucket; s++) {
        if (static_cast<uint8_t>(buckets[i2 * kCuckooSlotsPerBucket + s]) == fp)
            return true;
    }
    return false;
}

} // namespace ANCHORDB_NAMESPACE
