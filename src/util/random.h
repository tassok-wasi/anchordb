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

#include <cstdint>
namespace anchordb {

class Random {
  private:
    enum : uint32_t {
        M = 2147483647L // 2^31-1
    };
    enum : uint64_t {
        A = 16807 // bits 14, 8, 7, 5, 2, 1, 0
    };

    uint32_t seed_;

    static uint32_t good_seed(uint32_t s) { return (s & M) != 0 ? (s & M) : 1; }

  public:
    // This is the largest value that can be returned from next()
    enum : uint32_t { k_max_next = M };

    explicit Random(uint32_t s) : seed_(good_seed(s)) {}

    void reset(uint32_t s) { seed_ = good_seed(s); }

    uint32_t next() {
        // We are computing
        //       seed_ = (seed_ * A) % M,    where M = 2^31-1
        //
        // seed_ must not be zero or M, or else all subsequent computed values
        // will be zero or M respectively.  For all other values, seed_ will end
        // up cycling through every number in [1,M-1]
        uint64_t product = seed_ * A;

        // Compute (product % M) using the fact that ((x << 31) % M) == x.
        seed_ = static_cast<uint32_t>((product >> 31) + (product & M));
        // The first reduction may overflow by 1 bit, so we may need to
        // repeat.  mod == M is not possible; using > allows the faster
        // sign-bit-based test.
        if (seed_ > M) {
            seed_ -= M;
        }
        return seed_;
    }

    uint64_t next64() { return (uint64_t{next()} << 32) | next(); }

    // Returns a uniformly distributed value in the range [0..n-1]
    // REQUIRES: n > 0
    uint32_t uniform(int n) { return next() % n; }

    // Randomly returns true ~"1/n" of the time, and false otherwise.
    // REQUIRES: n > 0
    bool one_in(int n) { return uniform(n) == 0; }

    // skewed: pick "base" uniformly from range [0,max_log] and then
    // return "base" random bits.  The effect is to pick a number in the
    // range [0,2^max_log-1] with exponential bias towards smaller numbers.
    uint32_t skewed(int max_log) { return uniform(1 << uniform(max_log + 1)); }
};

} // namespace anchordb
