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

#include "coding.h"
#include <cstdint>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

// Encodes a 16-bit unsigned integer in little-endian byte order.
// Writes exactly 2 bytes to dst.
void encode_fixed_16(char *dst, uint16_t value) {
    uint8_t *buffer = reinterpret_cast<uint8_t *>(dst);

    buffer[0] = static_cast<uint8_t>(value);
    buffer[1] = static_cast<uint8_t>(value >> 8);
}

// Encodes a 32-bit unsigned integer in little-endian byte order.
// Writes exactly 4 bytes to dst.
void encode_fixed_32(char *dst, uint32_t value) {
    uint8_t *buffer = reinterpret_cast<uint8_t *>(dst);

    buffer[0] = static_cast<uint8_t>(value);
    buffer[1] = static_cast<uint8_t>(value >> 8);
    buffer[2] = static_cast<uint8_t>(value >> 16);
    buffer[3] = static_cast<uint8_t>(value >> 24);
}

// Encodes a 64-bit unsigned integer in little-endian byte order.
// Writes exactly 8 bytes to dst.
void encode_fixed_64(char *dst, uint64_t value) {
    uint8_t *buffer = reinterpret_cast<uint8_t *>(dst);

    buffer[0] = static_cast<uint8_t>(value);
    buffer[1] = static_cast<uint8_t>(value >> 8);
    buffer[2] = static_cast<uint8_t>(value >> 16);
    buffer[3] = static_cast<uint8_t>(value >> 24);
    buffer[4] = static_cast<uint8_t>(value >> 32);
    buffer[5] = static_cast<uint8_t>(value >> 40);
    buffer[6] = static_cast<uint8_t>(value >> 48);
    buffer[7] = static_cast<uint8_t>(value >> 56);
}

// Decodes a 16-bit unsigned integer from a little-endian byte sequence.
uint16_t decode_fixed_16(const char *ptr) {
    const uint8_t *buffer = reinterpret_cast<const uint8_t *>(ptr);

    return (static_cast<uint16_t>(buffer[0])) |
           (static_cast<uint16_t>(buffer[1]) << 8);
}

// Decodes a 32-bit unsigned integer from a little-endian byte sequence.
uint32_t decode_fixed_32(const char *ptr) {
    const uint8_t *buffer = reinterpret_cast<const uint8_t *>(ptr);

    return (static_cast<uint32_t>(buffer[0])) |
           (static_cast<uint32_t>(buffer[1]) << 8) |
           (static_cast<uint32_t>(buffer[2]) << 16) |
           (static_cast<uint32_t>(buffer[3]) << 24);
}

// Decodes a 64-bit unsigned integer from a little-endian byte sequence.
uint64_t decode_fixed_64(const char *ptr) {
    const uint8_t *buffer = reinterpret_cast<const uint8_t *>(ptr);

    return (static_cast<uint64_t>(buffer[0])) |
           (static_cast<uint64_t>(buffer[1]) << 8) |
           (static_cast<uint64_t>(buffer[2]) << 16) |
           (static_cast<uint64_t>(buffer[3]) << 24) |
           (static_cast<uint64_t>(buffer[4]) << 32) |
           (static_cast<uint64_t>(buffer[5]) << 40) |
           (static_cast<uint64_t>(buffer[6]) << 48) |
           (static_cast<uint64_t>(buffer[7]) << 56);
}

// Appends a 4-byte little-endian encoding of value to *dst.
void put_fixed_32(std::string *dst, uint32_t value) {
    char buf[sizeof(value)];
    encode_fixed_32(buf, value);
    dst->append(buf, sizeof(buf));
}

// Appends an 8-byte little-endian encoding of value to *dst.
void put_fixed_64(std::string *dst, uint64_t value) {
    char buf[sizeof(value)];
    encode_fixed_64(buf, value);
    dst->append(buf, sizeof(buf));
}

// Encodes v as a varint32 using base-128 little-endian.
// Each byte: lower 7 bits = data, MSB = continuation (1 = more bytes follow).
// Returns a pointer just past the last byte written.
char *encode_varint_32(char *dst, uint32_t v) {
    uint8_t *ptr = reinterpret_cast<uint8_t *>(dst);
    static const int B = 128;
    if (v < (1 << 7)) {
        *(ptr++) = v;
    } else if (v < (1 << 14)) {
        *(ptr++) = v | B;
        *(ptr++) = v >> 7;
    } else if (v < (1 << 21)) {
        *(ptr++) = v | B;
        *(ptr++) = (v >> 7) | B;
        *(ptr++) = v >> 14;
    } else if (v < (1 << 28)) {
        *(ptr++) = v | B;
        *(ptr++) = (v >> 7) | B;
        *(ptr++) = (v >> 14) | B;
        *(ptr++) = v >> 21;
    } else {
        *(ptr++) = v | B;
        *(ptr++) = (v >> 7) | B;
        *(ptr++) = (v >> 14) | B;
        *(ptr++) = (v >> 21) | B;
        *(ptr++) = v >> 28;
    }
    return reinterpret_cast<char *>(ptr);
}

// Encodes v as a varint64 using base-128 little-endian.
// Same scheme as varint32 but can produce up to 10 bytes.
char *encode_varint_64(char *dst, uint64_t v) {
    uint8_t *ptr = reinterpret_cast<uint8_t *>(dst);
    static const uint8_t B = 128;

    if (v < (1ull << 7)) {
        ptr[0] = static_cast<uint8_t>(v);
        return reinterpret_cast<char *>(ptr + 1);
    }
    if (v < (1ull << 14)) {
        ptr[0] = static_cast<uint8_t>(v | B);
        ptr[1] = static_cast<uint8_t>(v >> 7);
        return reinterpret_cast<char *>(ptr + 2);
    }
    if (v < (1ull << 21)) {
        ptr[0] = static_cast<uint8_t>(v | B);
        ptr[1] = static_cast<uint8_t>((v >> 7) | B);
        ptr[2] = static_cast<uint8_t>(v >> 14);
        return reinterpret_cast<char *>(ptr + 3);
    }
    if (v < (1ull << 28)) {
        ptr[0] = static_cast<uint8_t>(v | B);
        ptr[1] = static_cast<uint8_t>((v >> 7) | B);
        ptr[2] = static_cast<uint8_t>((v >> 14) | B);
        ptr[3] = static_cast<uint8_t>(v >> 21);
        return reinterpret_cast<char *>(ptr + 4);
    }
    if (v < (1ull << 35)) {
        ptr[0] = static_cast<uint8_t>(v | B);
        ptr[1] = static_cast<uint8_t>((v >> 7) | B);
        ptr[2] = static_cast<uint8_t>((v >> 14) | B);
        ptr[3] = static_cast<uint8_t>((v >> 21) | B);
        ptr[4] = static_cast<uint8_t>(v >> 28);
        return reinterpret_cast<char *>(ptr + 5);
    }
    if (v < (1ull << 42)) {
        ptr[0] = static_cast<uint8_t>(v | B);
        ptr[1] = static_cast<uint8_t>((v >> 7) | B);
        ptr[2] = static_cast<uint8_t>((v >> 14) | B);
        ptr[3] = static_cast<uint8_t>((v >> 21) | B);
        ptr[4] = static_cast<uint8_t>((v >> 28) | B);
        ptr[5] = static_cast<uint8_t>(v >> 35);
        return reinterpret_cast<char *>(ptr + 6);
    }
    if (v < (1ull << 49)) {
        ptr[0] = static_cast<uint8_t>(v | B);
        ptr[1] = static_cast<uint8_t>((v >> 7) | B);
        ptr[2] = static_cast<uint8_t>((v >> 14) | B);
        ptr[3] = static_cast<uint8_t>((v >> 21) | B);
        ptr[4] = static_cast<uint8_t>((v >> 28) | B);
        ptr[5] = static_cast<uint8_t>((v >> 35) | B);
        ptr[6] = static_cast<uint8_t>(v >> 42);
        return reinterpret_cast<char *>(ptr + 7);
    }
    if (v < (1ull << 56)) {
        ptr[0] = static_cast<uint8_t>(v | B);
        ptr[1] = static_cast<uint8_t>((v >> 7) | B);
        ptr[2] = static_cast<uint8_t>((v >> 14) | B);
        ptr[3] = static_cast<uint8_t>((v >> 21) | B);
        ptr[4] = static_cast<uint8_t>((v >> 28) | B);
        ptr[5] = static_cast<uint8_t>((v >> 35) | B);
        ptr[6] = static_cast<uint8_t>((v >> 42) | B);
        ptr[7] = static_cast<uint8_t>(v >> 49);
        return reinterpret_cast<char *>(ptr + 8);
    }
    if (v < (1ull << 63)) {
        ptr[0] = static_cast<uint8_t>(v | B);
        ptr[1] = static_cast<uint8_t>((v >> 7) | B);
        ptr[2] = static_cast<uint8_t>((v >> 14) | B);
        ptr[3] = static_cast<uint8_t>((v >> 21) | B);
        ptr[4] = static_cast<uint8_t>((v >> 28) | B);
        ptr[5] = static_cast<uint8_t>((v >> 35) | B);
        ptr[6] = static_cast<uint8_t>((v >> 42) | B);
        ptr[7] = static_cast<uint8_t>((v >> 49) | B);
        ptr[8] = static_cast<uint8_t>(v >> 56);
        return reinterpret_cast<char *>(ptr + 9);
    }

    // 10-byte fallback for values >= 2^63 (e.g. max uint64_t)
    ptr[0] = static_cast<uint8_t>(v | B);
    ptr[1] = static_cast<uint8_t>((v >> 7) | B);
    ptr[2] = static_cast<uint8_t>((v >> 14) | B);
    ptr[3] = static_cast<uint8_t>((v >> 21) | B);
    ptr[4] = static_cast<uint8_t>((v >> 28) | B);
    ptr[5] = static_cast<uint8_t>((v >> 35) | B);
    ptr[6] = static_cast<uint8_t>((v >> 42) | B);
    ptr[7] = static_cast<uint8_t>((v >> 49) | B);
    ptr[8] = static_cast<uint8_t>((v >> 56) | B);
    ptr[9] = static_cast<uint8_t>(v >> 63);
    return reinterpret_cast<char *>(ptr + 10);
}

// Appends a varint32 encoding of value to *dst.
void put_varint_32(std::string *dst, uint32_t value) {
    int size = 5;
    char buf[size];
    char *ptr = encode_varint_32(buf, value);
    dst->append(buf, ptr - buf);
}

// Appends a varint64 encoding of value to *dst.
void put_varint_64(std::string *dst, uint64_t value) {
    int size = 10;
    char buf[size];
    char *ptr = encode_varint_64(buf, value);
    dst->append(buf, ptr - buf);
}

// Encodes a std::string_view as: | varint32(size) | raw bytes |.
void put_len_prefixed_slice(std::string *dst, std::string_view value) {
    put_varint_32(dst, value.size());
    dst->append(value.data(), value.size());
}

// Returns the number of bytes required to encode v as a varint.
int varint_len(uint64_t v) {
    int len = 1;
    while (v >= 128) {
        v >>= 7;
        len++;
    }
    return len;
}

const char *get_varint_32_ptr_fallback(const char *p, const char *limit,
                                       uint32_t *value) {
    const auto *ptr = reinterpret_cast<const uint8_t *>(p);
    const auto *lim = reinterpret_cast<const uint8_t *>(limit);

    if (ptr >= lim) return nullptr;

    uint32_t b0 = *ptr++;
    uint32_t result = b0 & 0x7f;
    if ((b0 & 0x80) == 0) {
        *value = result;
        return reinterpret_cast<const char *>(ptr);
    }

    if (ptr >= lim) return nullptr;
    uint32_t b1 = *ptr++;
    result |= (b1 & 0x7f) << 7;
    if ((b1 & 0x80) == 0) {
        *value = result;
        return reinterpret_cast<const char *>(ptr);
    }

    if (ptr >= lim) return nullptr;
    uint32_t b2 = *ptr++;
    result |= (b2 & 0x7f) << 14;
    if ((b2 & 0x80) == 0) {
        *value = result;
        return reinterpret_cast<const char *>(ptr);
    }

    if (ptr >= lim) return nullptr;
    uint32_t b3 = *ptr++;
    result |= (b3 & 0x7f) << 21;
    if ((b3 & 0x80) == 0) {
        *value = result;
        return reinterpret_cast<const char *>(ptr);
    }

    if (ptr >= lim) return nullptr;
    uint32_t b4 = *ptr++;
    result |= b4 << 28; // 5th byte holds the remaining bits
    if ((b4 & 0x80) == 0) {
        *value = result;
        return reinterpret_cast<const char *>(ptr);
    }

    return nullptr; // Corrupted varint (spans > 5 bytes for uint32)
}

const char *get_varint_64_ptr_fallback(const char *p, const char *limit,
                                       uint64_t *value) {
    const auto *ptr = reinterpret_cast<const uint8_t *>(p);
    const auto *lim = reinterpret_cast<const uint8_t *>(limit);

    if (ptr >= lim) return nullptr;

    // Unroll the first 3 bytes (handles values up to ~2 million)
    uint64_t b0 = *ptr++;
    uint64_t result = b0 & 0x7f;
    if ((b0 & 0x80) == 0) {
        *value = result;
        return reinterpret_cast<const char *>(ptr);
    }

    if (ptr >= lim) return nullptr;
    uint64_t b1 = *ptr++;
    result |= (b1 & 0x7f) << 7;
    if ((b1 & 0x80) == 0) {
        *value = result;
        return reinterpret_cast<const char *>(ptr);
    }

    if (ptr >= lim) return nullptr;
    uint64_t b2 = *ptr++;
    result |= (b2 & 0x7f) << 14;
    if ((b2 & 0x80) == 0) {
        *value = result;
        return reinterpret_cast<const char *>(ptr);
    }

    // Hybrid loop fallback for bytes 4 through 10
    uint32_t shift = 21;
    while (ptr < lim && shift <= 63) {
        uint64_t b = *ptr++;
        result |= (b & 0x7f) << shift;
        if ((b & 0x80) == 0) {
            *value = result;
            return reinterpret_cast<const char *>(ptr);
        }
        shift += 7;
    }

    return nullptr; // Corrupted varint or buffer overflow
}

const char *get_varint_32_ptr(const char *p, const char *limit,
                              uint32_t *value) {
    if (p < limit) {
        uint32_t result = *reinterpret_cast<const uint8_t *>(p);
        if ((result & 0x80) == 0) {
            *value = result;
            return p + 1;
        }
    }
    return get_varint_32_ptr_fallback(p, limit, value);
}

const char *get_varint_64_ptr(const char *p, const char *limit,
                              uint64_t *value) {
    if (p < limit) {
        uint64_t result = *reinterpret_cast<const uint8_t *>(p);
        if ((result & 0x80) == 0) {
            *value = result;
            return p + 1;
        }
    }
    return get_varint_64_ptr_fallback(p, limit, value);
}

bool get_varint_32(std::string_view *input, uint32_t *value) {
    const char *p = input->data();
    const char *limit = p + input->size();
    const char *ptr = get_varint_32_ptr(p, limit, value);
    if (ptr == nullptr) { return false; }
    *input = std::string_view(ptr, limit - ptr);
    return true;
}

bool get_varint_64(std::string_view *input, uint64_t *value) {
    const char *p = input->data();
    const char *limit = p + input->size();
    const char *ptr = get_varint_64_ptr(p, limit, value);
    if (ptr == nullptr) { return false; }
    *input = std::string_view(ptr, limit - ptr);
    return true;
}

// Reads a length-prefixed slice from *input.  On success, *result holds the
// slice data and *input is advanced past it.  Returns false if the input is
// truncated (varint can't be decoded, or not enough bytes for the payload).
bool get_len_prefixed_slice(std::string_view *input, std::string_view *result) {
    uint32_t size;
    if (get_varint_32(input, &size) && input->size() >= size) {
        *result = std::string_view(input->data(), size);
        input->remove_prefix(size);
        return true;
    } else {
        return false;
    }
}

} // namespace ANCHORDB_NAMESPACE
