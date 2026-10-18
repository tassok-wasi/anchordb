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

#include "hash.h"
#include "util/coding.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

uint32_t murmurhash2_32(const char *data, size_t n, uint32_t seed) {
    const uint32_t m = 0xc6a4a793;
    const uint32_t r = 24;
    const char *limit = data + n;
    uint32_t h = seed ^ (n * m);

    // Unroll loop to process 8 bytes at a time for better ILP
    while (limit - data >= 8) {
        uint32_t w1 = decode_fixed_32(data);
        uint32_t w2 = decode_fixed_32(data + 4);
        data += 8;
        h += w1;
        h *= m;
        h ^= (h >> 16);
        h += w2;
        h *= m;
        h ^= (h >> 16);
    }

    if (limit - data >= 4) {
        uint32_t w = decode_fixed_32(data);
        data += 4;
        h += w;
        h *= m;
        h ^= (h >> 16);
    }

    // Pick up remaining bytes
    switch (limit - data) {
    case 3:
        h += static_cast<uint32_t>(static_cast<uint8_t>(data[2])) << 16;
        [[fallthrough]];
    case 2:
        h += static_cast<uint32_t>(static_cast<uint8_t>(data[1])) << 8;
        [[fallthrough]];
    case 1:
        h += static_cast<uint32_t>(static_cast<uint8_t>(data[0]));
        h *= m;
        h ^= (h >> r);
        break;
    }
    return h;
}

uint64_t xx_hash64(std::string_view slice, uint64_t seed) {
    const uint8_t *p = reinterpret_cast<const uint8_t *>(slice.data());
    size_t len = slice.size();

    static constexpr uint64_t PRIME64_1 = 11400714785074694791ULL;
    static constexpr uint64_t PRIME64_2 = 14029467366897019727ULL;
    static constexpr uint64_t PRIME64_3 = 1609587929392839161ULL;
    static constexpr uint64_t PRIME64_4 = 9650029242287828579ULL;
    static constexpr uint64_t PRIME64_5 = 2870177450012600261ULL;

    uint64_t h64;

    if (len >= 32) {
        uint64_t v1 = seed + PRIME64_1 + PRIME64_2;
        uint64_t v2 = seed + PRIME64_2;
        uint64_t v3 = seed + 0;
        uint64_t v4 = seed - PRIME64_1;

        const uint8_t *const limit = p + len - 32;
        do {
            auto round = [](uint64_t acc, uint64_t input) {
                acc += input * PRIME64_2;
                acc = (acc << 31) | (acc >> (64 - 31));
                acc *= PRIME64_1;
                return acc;
            };

            uint64_t u1, u2, u3, u4;
            std::memcpy(&u1, p, 8);
            std::memcpy(&u2, p + 8, 8);
            std::memcpy(&u3, p + 16, 8);
            std::memcpy(&u4, p + 24, 8);

            v1 = round(v1, u1);
            v2 = round(v2, u2);
            v3 = round(v3, u3);
            v4 = round(v4, u4);
            p += 32;
        } while (p <= limit);

        auto merge = [](uint64_t acc, uint64_t val) {
            val *= PRIME64_2;
            val = (val << 31) | (val >> (64 - 31));
            val *= PRIME64_1;
            acc ^= val;
            acc = acc * PRIME64_1 + PRIME64_4;
            return acc;
        };

        h64 = ((v1 << 1) | (v1 >> 63)) + ((v2 << 7) | (v2 >> 57)) +
              ((v3 << 12) | (v3 >> 52)) + ((v4 << 18) | (v4 >> 46));
        h64 = merge(h64, v1);
        h64 = merge(h64, v2);
        h64 = merge(h64, v3);
        h64 = merge(h64, v4);
    } else {
        h64 = seed + PRIME64_5;
    }

    h64 += static_cast<uint64_t>(len);

    // Process 8-byte chunks
    while (len >= 8) {
        uint64_t k1;
        std::memcpy(&k1, p, 8);
        k1 *= PRIME64_2;
        k1 = (k1 << 31) | (k1 >> (64 - 31));
        k1 *= PRIME64_1;
        h64 ^= k1;
        h64 = ((h64 << 27) | (h64 >> (64 - 27))) * PRIME64_1 + PRIME64_4;
        p += 8;
        len -= 8;
    }

    // Process 4-byte chunks
    if (len >= 4) {
        uint32_t k1;
        std::memcpy(&k1, p, 4);
        h64 ^= static_cast<uint64_t>(k1) * PRIME64_1;
        h64 = ((h64 << 23) | (h64 >> (64 - 23))) * PRIME64_2 + PRIME64_3;
        p += 4;
        len -= 4;
    }

    // Process remaining 1-3 bytes
    while (len > 0) {
        h64 ^= static_cast<uint64_t>(*p) * PRIME64_5;
        h64 = ((h64 << 11) | (h64 >> (64 - 11))) * PRIME64_1;
        p++;
        len--;
    }

    // Avalanche finalizer
    h64 ^= h64 >> 33;
    h64 *= PRIME64_2;
    h64 ^= h64 >> 29;
    h64 *= PRIME64_3;
    h64 ^= h64 >> 32;

    return h64;
}

uint64_t murmurhash3_64(std::string_view slice, uint64_t seed) {
    const char *data = slice.data();
    const size_t len = slice.size();

    uint64_t h1 = seed;
    uint64_t h2 = seed;

    static constexpr uint64_t c1 = 0x87c37b91114253d5ULL;
    static constexpr uint64_t c2 = 0x4cf5ad432745937fULL;

    const char *limit = data + len;

    while (limit - data >= 16) {
        uint64_t k1 = decode_fixed_64(data);
        uint64_t k2 = decode_fixed_64(data + 8);
        data += 16;

        // Pipe 1 (k1)
        k1 *= c1;
        k1 = (k1 << 31) | (k1 >> (64 - 31));
        k1 *= c2;
        h1 ^= k1;
        h1 = (h1 << 27) | (h1 >> (64 - 27));
        h1 = h1 * 5 + 0x52dce729;

        // Pipe 2 (k2)
        k2 *= c2;
        k2 = (k2 << 33) | (k2 >> (64 - 33));
        k2 *= c1;
        h2 ^= k2;
        h2 = (h2 << 31) | (h2 >> (64 - 31));
        h2 = h2 * 5 + 0x38495ab5;
    }

    if (limit - data >= 8) {
        uint64_t k1 = decode_fixed_64(data);
        data += 8;

        k1 *= c1;
        k1 = (k1 << 31) | (k1 >> (64 - 31));
        k1 *= c2;
        h1 ^= k1;
        h1 = (h1 << 27) | (h1 >> (64 - 27));
        h1 = h1 * 5 + 0x52dce729;
    }

    uint64_t k1 = 0;
    size_t remaining = limit - data;

    switch (remaining) {
    case 7:
        k1 ^= static_cast<uint64_t>(static_cast<uint8_t>(data[6])) << 48;
        [[fallthrough]];
    case 6:
        k1 ^= static_cast<uint64_t>(static_cast<uint8_t>(data[5])) << 40;
        [[fallthrough]];
    case 5:
        k1 ^= static_cast<uint64_t>(static_cast<uint8_t>(data[4])) << 32;
        [[fallthrough]];
    case 4:
        k1 ^= static_cast<uint64_t>(static_cast<uint8_t>(data[3])) << 24;
        [[fallthrough]];
    case 3:
        k1 ^= static_cast<uint64_t>(static_cast<uint8_t>(data[2])) << 16;
        [[fallthrough]];
    case 2:
        k1 ^= static_cast<uint64_t>(static_cast<uint8_t>(data[1])) << 8;
        [[fallthrough]];
    case 1:
        k1 ^= static_cast<uint64_t>(static_cast<uint8_t>(data[0]));
        k1 *= c1;
        k1 = (k1 << 31) | (k1 >> (64 - 31));
        k1 *= c2;
        h1 ^= k1;
        break;
    }

    h1 ^= len;
    h2 ^= len;

    h1 += h2;
    h2 += h1;

    // Finalizer avalanche for h1
    h1 ^= h1 >> 33;
    h1 *= 0xff51afd7ed558ccdULL;
    h1 ^= h1 >> 33;
    h1 *= 0xc4ceb9fe1a85ec53ULL;
    h1 ^= h1 >> 33;

    return h1;
}

} // namespace anchordb
