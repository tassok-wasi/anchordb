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

namespace anchordb {

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
    while (v >= B) {
        *(ptr++) = (v & 0x7f) | B;
        v >>= 7;
    }
    *(ptr++) = v;
    return reinterpret_cast<char *>(ptr);
}

// Encodes v as a varint64 using base-128 little-endian.
// Same scheme as varint32 but can produce up to 10 bytes.
char *encode_varint_64(char *dst, uint64_t v) {
    uint8_t *ptr = reinterpret_cast<uint8_t *>(dst);
    static const int B = 128;
    while (v >= B) {
        *(ptr++) = (v & 0x7f) | B;
        v >>= 7;
    }
    *(ptr++) = v;
    return reinterpret_cast<char *>(ptr);
}

// Decodes a varint32 from ptr.  No bounds checking — caller must guarantee
// the varint is complete and well-formed.
uint32_t decode_varint_32(const char *ptr) {
    uint32_t result = 0;
    const uint8_t *p = reinterpret_cast<const uint8_t *>(ptr);
    while (*p & 128) {
        result |= (*p & 0x7f);
        p++;
    }
    result |= *p;
    return result;
}

// Decodes a varint64 from ptr.  No bounds checking — caller must guarantee
// the varint is complete and well-formed.
uint64_t decode_varint_64(const char *ptr) {
    uint64_t result = 0;
    const uint8_t *p = reinterpret_cast<const uint8_t *>(ptr);
    while (*p & 128) {
        result |= (*p & 0x7f);
        p++;
    }
    result |= *p;
    return result;
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

// Encodes a Slice as: | varint32(size) | raw bytes |.
void put_len_prefixed_slice(std::string *dst, const Slice &value) {
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

// Fallback varint32 decoder with bounds checking.  Reads bytes from [p, limit),
// stopping when a byte with MSB clear is found.  Returns nullptr if the varint
// is truncated (no terminator byte before limit).
const char *get_varint_32_ptr_fallback(const char *p, const char *limit,
                                       uint32_t *value) {
    uint32_t result = 0;
    const uint8_t *ptr = reinterpret_cast<const uint8_t *>(p);
    while (ptr < reinterpret_cast<const uint8_t *>(limit) && *ptr & 128) {
        result |= (*ptr & 0x7f);
        ptr++;
    }
    if (ptr == reinterpret_cast<const uint8_t *>(limit)) {
        return nullptr;
    }
    result |= *ptr;
    *value = result;
    return reinterpret_cast<const char *>(ptr + 1);
}

// Fallback varint64 decoder with bounds checking.  Same logic as the 32-bit
// version but returns a uint64_t.
const char *get_varint_64_ptr_fallback(const char *p, const char *limit,
                                       uint64_t *value) {
    uint64_t result = 0;
    const uint8_t *ptr = reinterpret_cast<const uint8_t *>(p);
    while (ptr < reinterpret_cast<const uint8_t *>(limit) && *ptr & 128) {
        result |= (*ptr & 0x7f);
        ptr++;
    }
    if (ptr == reinterpret_cast<const uint8_t *>(limit)) {
        return nullptr;
    }
    result |= *ptr;
    *value = result;
    return reinterpret_cast<const char *>(ptr + 1);
}

// Bounds-checked varint32 decoder with a fast path: if the first byte has
// MSB clear, the varint is a single byte and we return immediately.
// Otherwise falls back to get_varint_32_ptr_fallback.
const char *get_varint_32_ptr(const char *p, const char *limit,
                              uint32_t *value) {
    if (p < limit) {
        uint32_t result = *(reinterpret_cast<const uint8_t *>(p));
        if ((result & 128) == 0) {
            *value = result;
            return p + 1;
        }
    }
    return get_varint_32_ptr_fallback(p, limit, value);
}

// Bounds-checked varint64 decoder with the same fast-path optimization.
const char *get_varint_64_ptr(const char *p, const char *limit,
                              uint64_t *value) {
    if (p < limit) {
        uint64_t result = *(reinterpret_cast<const uint8_t *>(p));
        if ((result & 128) == 0) {
            *value = result;
            return p + 1;
        }
    }
    return get_varint_64_ptr_fallback(p, limit, value);
}

// Decodes a varint32 from the front of *input, advancing the Slice past the
// consumed bytes.  Returns false if the varint is truncated.
bool get_varint_32(Slice *input, uint32_t *value) {
    const char *p = input->data();
    const char *limit = p + input->size();
    const char *ptr = get_varint_32_ptr(p, limit, value);
    if (ptr == nullptr) {
        return false;
    } else {
        *input = Slice(ptr, limit - ptr);
        return true;
    }
}

// Decodes a varint64 from the front of *input, advancing the Slice past the
// consumed bytes.  Returns false if the varint is truncated.
bool get_varint_64(Slice *input, uint64_t *value) {
    const char *p = input->data();
    const char *limit = p + input->size();
    const char *ptr = get_varint_64_ptr(p, limit, value);
    if (ptr == nullptr) {
        return false;
    } else {
        *input = Slice(ptr, limit - ptr);
        return true;
    }
}

// Reads a length-prefixed slice from *input.  On success, *result holds the
// slice data and *input is advanced past it.  Returns false if the input is
// truncated (varint can't be decoded, or not enough bytes for the payload).
bool get_len_prefixed_slice(Slice *input, Slice *result) {
    uint32_t size;
    if (get_varint_32(input, &size) && input->size() >= size) {
        *result = Slice(input->data(), size);
        input->remove_prefix(size);
        return true;
    } else {
        return false;
    }
}

} // namespace anchordb
