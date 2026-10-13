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

#include <cassert>
#include <cstdint>

namespace anchordb {

// A small, deterministic pseudo-random number generator built on the
// Park-Miller "minimal standard" linear congruential generator:
//
//     state = (state * 16807) mod (2^31 - 1)
//
// The generator is fast, has a full period of 2^31 - 2, and produces the same
// sequence on every platform for a given seed. It is intended for internal
// use such as randomized data structures; it is NOT cryptographically secure
// and must never be used for security-sensitive purposes.
class Random {
  public:
    // The largest value that next() can return.
    static constexpr uint32_t k_max_next = 2147483647u; // 2^31 - 1

    explicit Random(uint32_t seed) : state_(normalize_seed(seed)) {}

    // Re-seeds the generator. Seeds of 0 (or any multiple of the modulus) are
    // remapped to a valid state so the generator never degenerates to a
    // constant sequence.
    void reset(uint32_t seed) { state_ = normalize_seed(seed); }

    // Returns the next value in the range [1, k_max_next].
    uint32_t next() {
        // Multiply in 64 bits so the product cannot overflow, then reduce
        // modulo 2^31 - 1 using the identity (x << 31) % M == x.
        const uint64_t product = static_cast<uint64_t>(state_) * k_multiplier;
        uint32_t reduced =
            static_cast<uint32_t>((product >> 31) + (product & k_modulus));
        // The first reduction can overflow by a single bit, so a second
        // correction may be required. The result can never equal the modulus,
        // so a strict ">" comparison is sufficient.
        if (reduced > k_modulus) {
            reduced -= k_modulus;
        }
        state_ = reduced;
        return state_;
    }

    // Returns a 64-bit value assembled from two consecutive 32-bit draws.
    uint64_t next64() { return (static_cast<uint64_t>(next()) << 32) | next(); }

    // Returns a uniformly distributed value in the range [0, n).
    // REQUIRES: n > 0
    uint32_t uniform(uint32_t n) {
        assert(n > 0);
        if (n == 0) {
            return 0;
        }

        // A single draw cannot cover a range wider than the generator's own
        // range, so rejection sampling would never terminate. Fall back to a
        // modulo reduction; the resulting bias is negligible for such large n
        // (the only power-of-two case is n == 2^31).
        if (n > k_max_next) {
            return next() % n;
        }

        // Plain "next() % n" is biased whenever n does not divide the
        // generator's range. Reject the tail of the range so that only a whole
        // number of n-sized buckets is accepted, which yields an exactly
        // uniform result. Here n <= k_max_next, so limit >= 1 and the loop
        // always terminates.
        const uint32_t limit = k_max_next - (k_max_next % n);
        uint32_t value;
        do {
            value =
                next() - 1; // Shift [1, k_max_next] down to [0, k_max_next).
        } while (value >= limit);
        return value % n;
    }

    // Returns true with probability approximately 1/n, false otherwise.
    // REQUIRES: n > 0
    bool one_in(uint32_t n) {
        assert(n > 0);
        return n > 0 && uniform(n) == 0;
    }

    // Picks a bit-width uniformly from [0, max_log] and returns a value with
    // that many random bits. The effect is to pick a number in [0, 2^max_log)
    // with an exponential bias toward smaller values.
    // REQUIRES: 0 <= max_log < 32
    uint32_t skewed(int max_log) {
        assert(max_log >= 0 && max_log < 32);
        if (max_log < 0) {
            max_log = 0;
        } else if (max_log > 31) {
            max_log = 31;
        }

        const uint32_t bits = uniform(static_cast<uint32_t>(max_log) + 1);
        if (bits == 0) {
            return 0;
        }
        return uniform(1u << bits);
    }

  private:
    static constexpr uint32_t k_modulus = 2147483647u; // 2^31 - 1
    static constexpr uint32_t k_multiplier = 16807u;

    // Maps an arbitrary seed into the valid state range [1, k_modulus - 1].
    static uint32_t normalize_seed(uint32_t seed) {
        const uint32_t masked = seed & k_modulus;
        return masked != 0 ? masked : 1;
    }

    uint32_t state_;
};

} // namespace anchordb
