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
#include <string>
#include <string_view>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

// Writes a 16-bit unsigned integer to dst in little-endian byte order.
void encode_fixed_16(char *dst, uint16_t vlaue);
// Writes a 32-bit unsigned integer to dst in little-endian byte order.
void encode_fixed_32(char *dst, uint32_t value);
// Writes a 64-bit unsigned integer to dst in little-endian byte order.
void encode_fixed_64(char *dst, uint64_t value);

// Reads a 16-bit unsigned integer from ptr (little-endian).
uint16_t decode_fixed_16(const char *ptr);
// Reads a 32-bit unsigned integer from ptr (little-endian).
uint32_t decode_fixed_32(const char *ptr);
// Reads a 64-bit unsigned integer from ptr (little-endian).
uint64_t decode_fixed_64(const char *ptr);

// Varint encoding (variable-length, little-endian base-128)
// Each byte uses 7 bits for data and 1 continuation bit (MSB).
// The MSB is set on all bytes except the last, indicating more bytes follow.
// This is compact for small values: a uint32_t takes 1–5 bytes, a uint64_t
// 1–10.

// Encodes v as a varint32 into dst.  Returns a pointer just past the last byte
// written, suitable for chaining.
char *encode_varint_32(char *dst, uint32_t v);
// Encodes v as a varint64 into dst.  Returns a pointer just past the last byte.
char *encode_varint_64(char *dst, uint64_t v);

// Appends a 4-byte little-endian encoding of value to *dst.
void put_fixed_32(std::string *dst, uint32_t value);
// Appends an 8-byte little-endian encoding of value to *dst.
void put_fixed_64(std::string *dst, uint64_t value);

// Appends a varint32 encoding of value to *dst.
void put_varint_32(std::string *dst, uint32_t value);
// Appends a varint64 encoding of value to *dst.
void put_varint_64(std::string *dst, uint64_t value);

// Returns the number of bytes needed to encode v as a varint.
int varint_len(uint64_t v);

// These read a varint from [p, limit), returning a pointer to the byte after
// the varint on success, or nullptr if the varint is truncated.

const char *get_varint_32_ptr_fallback(const char *p, const char *limit,
                                       uint32_t *value);
const char *get_varint_64_ptr_fallback(const char *p, const char *limit,
                                       uint64_t *value);

// Fast-path: if the first byte has MSB clear, the varint is a single byte.
// Otherwise falls back to the general (slower) decoder.
const char *get_varint_32_ptr(const char *p, const char *limit,
                              uint32_t *value);
const char *get_varint_64_ptr(const char *p, const char *limit,
                              uint64_t *value);

// Decodes a varint from the front of *input, advancing the std::string_view
// past the consumed bytes.  Returns false if the varint is truncated.
bool get_varint_32(std::string_view *input, uint32_t *value);
bool get_varint_64(std::string_view *input, uint64_t *value);

// Length-prefixed slice encoding
// Encodes a std::string_view as: | varint32(size) | raw bytes |.
// This is the standard way to embed variable-length data in a binary record.

// Appends a length-prefixed slice to *dst.
void put_len_prefixed_slice(std::string *dst, std::string_view value);
// Reads a length-prefixed slice from the front of *input, advancing past it.
// Returns false if the input is truncated.
bool get_len_prefixed_slice(std::string_view *input, std::string_view *result);

} // namespace ANCHORDB_NAMESPACE
