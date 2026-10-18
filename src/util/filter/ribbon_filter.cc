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

#include "ribbon_filter.h"
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>
#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

void ribbon_create(const void *state_ptr, const std::string_view *keys,
                   size_t n, std::string *dst) {
    if (n == 0)
        return;

    auto *state = static_cast<const RibbonState *>(state_ptr);

    // Number of columns.  Must be strictly greater than the ribbon width so
    // that every key has a valid start column.
    size_t num_bits = n * state->bits_per_key;
    if (num_bits < k_ribbon_width + 1)
        num_bits = k_ribbon_width + 1;
    // Round up to a byte boundary so the serialized payload is whole bytes.
    num_bits = (num_bits + 7) & ~static_cast<size_t>(7);

    // Build the linear system
    std::vector<size_t> start(n);
    std::vector<uint64_t> mask(n);
    std::vector<uint8_t> rhs(n); // 8-bit fingerprint = RHS for 8 systems

    for (size_t i = 0; i < n; i++) {
        mask[i] = ribbon_mask(keys[i]);
        rhs[i] = ribbon_fingerprint(keys[i]);
        start[i] = ribbon_start(keys[i], num_bits);
    }

    // Sort row indices by start column — required for banded elimination.
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; i++)
        order[i] = i;
    std::sort(order.begin(), order.end(),
              [&](size_t a, size_t b) { return start[a] < start[b]; });

    // Banded Gaussian elimination (forward pass)
    constexpr size_t k_no_pivot = std::numeric_limits<size_t>::max();
    std::vector<size_t> pivot_row(num_bits, k_no_pivot);

    for (size_t idx : order) {
        uint64_t row = mask[idx];
        uint8_t target = rhs[idx];
        const size_t base = start[idx];

        while (row != 0) {
            const int b = std::countr_zero(row); // lowest set bit
            const size_t col = base + static_cast<size_t>(b);
            const size_t p = pivot_row[col];

            if (p != k_no_pivot) {
                // Column already has a pivot — eliminate this bit.
                row ^= mask[p];
                target ^= rhs[p];
            } else {
                // Free column — record as pivot and store the reduced row.
                pivot_row[col] = idx;
                mask[idx] = row;
                rhs[idx] = target;
                break;
            }
        }
    }

    // Back substitution
    std::vector<uint8_t> z(num_bits, 0);

    for (size_t col = num_bits; col > 0; --col) {
        const size_t c = col - 1;
        const size_t p = pivot_row[c];

        if (p == k_no_pivot) {
            z[c] = 0;
            continue;
        }

        uint8_t val = rhs[p];
        const size_t base = start[p];
        const uint64_t row = mask[p];

        // XOR in z[base + b] for every b > pivot_position where row has bit b.
        // (Bits below the pivot are guaranteed zero after forward elimination.)
        for (size_t b = 0; b < k_ribbon_width; b++) {
            const size_t pos = base + b;
            if (pos > c && (row & (1ULL << b)) != 0) {
                val ^= z[pos];
            }
        }
        z[c] = val;
    }

    // Serialize
    // Layout: [fingerprint-bits (1 byte)][z[0] … z[num_bits-1]]
    const size_t init_size = dst->size();
    dst->resize(init_size + 1 + num_bits, 0);
    char *array = &(*dst)[init_size];
    array[0] = static_cast<char>(k_ribbon_fingerprint_bits);
    for (size_t i = 0; i < num_bits; i++) {
        array[1 + i] = static_cast<char>(z[i]);
    }
}

bool ribbon_match(const void *state_ptr, const std::string_view &key,
                  const std::string_view &ribbon_filter) {
    const size_t len = ribbon_filter.size();
    // Need at least 1 header byte + enough room for a ribbon.
    if (len <= k_ribbon_width + 1)
        return false;

    const char *data = ribbon_filter.data();
    const size_t num_bits = len - 1; // exclude the header byte

    const uint8_t fp = ribbon_fingerprint(key);
    const size_t base = ribbon_start(key, num_bits);
    const uint64_t mask = ribbon_mask(key);

    // Compute the dot product for all 8 fingerprint bits simultaneously:
    // each solution byte packs bits 0..7 for that column, so XOR-ing the
    // bytes selected by the ribbon mask yields the 8-bit result.
    uint8_t result = 0;
    uint64_t m = mask;
    while (m != 0) {
        const int b = std::countr_zero(m);
        m &= m - 1; // clear lowest set bit
        result ^= static_cast<uint8_t>(data[1 + base + static_cast<size_t>(b)]);
    }

    return result == fp;
}

} // namespace anchordb
