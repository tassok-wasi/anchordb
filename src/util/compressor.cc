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

#include <cstddef>
#include <cstdint>
#include <string>

#include "anchordb/compressor.h"
#include "port/port.h" // IWYU pragma: keep
#include "util/coding.h"

namespace anchordb {

namespace {

#if HAS_ZSTD
// Returns the uncompressed size of the Zstd frame in [input, input + length).
// Returns false if the frame is invalid or does not record its content size.
bool zstd_get_uncompressed_length(const char *input, size_t length,
                                  size_t *result) {
    unsigned long long size = ZSTD_getFrameContentSize(input, length);
    if (size == ZSTD_CONTENTSIZE_UNKNOWN || size == ZSTD_CONTENTSIZE_ERROR) {
        return false;
    }
    *result = static_cast<size_t>(size);
    return true;
}
#endif // HAS_ZSTD

bool snappy_compress(const char *input, size_t length, std::string *output,
                     int level) {
#if HAS_SNAPPY
    output->resize(snappy::MaxCompressedLength(length));
    size_t outlen = 0;
    snappy::RawCompress(input, length, &(*output)[0], &outlen);
    output->resize(outlen);
    return true;
#else
    (void)input;
    (void)length;
    (void)output;
    (void)level;
    return false;
#endif
}

bool snappy_uncompress(const char *input, size_t length, char *output) {
#if HAS_SNAPPY
    return snappy::RawUncompress(input, length, output);
#else
    (void)input;
    (void)length;
    (void)output;
    return false;
#endif
}

bool get_snappy_uncompressed_length(const char *input, size_t length,
                                    size_t *result) {
#if HAS_SNAPPY
    return snappy::GetUncompressedLength(input, length, result);
#else
    (void)input;
    (void)length;
    (void)result;
    return false;
#endif
}

bool zstd_compress(const char *input, size_t length, std::string *output,
                   int level) {
#if HAS_ZSTD
    size_t outlen = ZSTD_compressBound(length);
    output->resize(outlen);

    ZSTD_CCtx *ctx = ZSTD_createCCtx();
    if (ctx == nullptr) {
        return false;
    }
    if (ZSTD_isError(
            ZSTD_CCtx_setParameter(ctx, ZSTD_c_compressionLevel, level))) {
        ZSTD_freeCCtx(ctx);
        return false;
    }
    outlen = ZSTD_compress2(ctx, &(*output)[0], output->size(), input, length);
    ZSTD_freeCCtx(ctx);
    if (ZSTD_isError(outlen)) {
        return false;
    }
    output->resize(outlen);
    return true;
#else
    (void)input;
    (void)length;
    (void)output;
    (void)level;
    return false;
#endif
}

bool zstd_uncompress(const char *input, size_t length, char *output) {
#if HAS_ZSTD
    size_t outlen = 0;
    if (!zstd_get_uncompressed_length(input, length, &outlen)) {
        return false;
    }

    ZSTD_DCtx *ctx = ZSTD_createDCtx();
    if (ctx == nullptr) {
        return false;
    }
    size_t result = ZSTD_decompressDCtx(ctx, output, outlen, input, length);
    ZSTD_freeDCtx(ctx);
    if (ZSTD_isError(result)) {
        return false;
    }
    return true;
#else
    (void)input;
    (void)length;
    (void)output;
    return false;
#endif
}

bool get_zstd_uncompressed_length(const char *input, size_t length,
                                  size_t *result) {
#if HAS_ZSTD
    return zstd_get_uncompressed_length(input, length, result);
#else
    (void)input;
    (void)length;
    (void)result;
    return false;
#endif
}

#if HAS_LZ4
// Returns the uncompressed size recorded in the LZ4 frame header in
// [input, input + length). Returns false if the frame is invalid or does not
// record its content size.
bool lz4_get_uncompressed_length(const char *input, size_t length,
                                 size_t *result) {
    LZ4F_dctx *ctx = nullptr;
    if (LZ4F_isError(LZ4F_createDecompressionContext(&ctx, LZ4F_VERSION))) {
        return false;
    }
    LZ4F_frameInfo_t info = LZ4F_INIT_FRAMEINFO;
    size_t src_size = length;
    size_t ret = LZ4F_getFrameInfo(ctx, &info, input, &src_size);
    LZ4F_freeDecompressionContext(ctx);
    if (LZ4F_isError(ret) || info.contentSize == 0) {
        return false;
    }
    *result = static_cast<size_t>(info.contentSize);
    return true;
}
#endif // HAS_LZ4

bool lz4_compress(const char *input, size_t length, std::string *output,
                  int level) {
#if HAS_LZ4
    LZ4F_preferences_t prefs = LZ4F_INIT_PREFERENCES;
    prefs.compressionLevel = level;
    prefs.frameInfo.contentSize = length;

    size_t bound = LZ4F_compressFrameBound(length, &prefs);
    output->resize(bound);
    size_t outlen =
        LZ4F_compressFrame(&(*output)[0], bound, input, length, &prefs);
    if (LZ4F_isError(outlen)) {
        return false;
    }
    output->resize(outlen);
    return true;
#else
    (void)input;
    (void)length;
    (void)output;
    (void)level;
    return false;
#endif
}

bool lz4_uncompress(const char *input, size_t length, char *output) {
#if HAS_LZ4
    size_t outlen = 0;
    if (!lz4_get_uncompressed_length(input, length, &outlen)) {
        return false;
    }

    LZ4F_dctx *ctx = nullptr;
    if (LZ4F_isError(LZ4F_createDecompressionContext(&ctx, LZ4F_VERSION))) {
        return false;
    }
    size_t dst_size = outlen;
    size_t src_size = length;
    size_t ret =
        LZ4F_decompress(ctx, output, &dst_size, input, &src_size, nullptr);
    LZ4F_freeDecompressionContext(ctx);
    if (LZ4F_isError(ret) || ret != 0) {
        return false;
    }
    return true;
#else
    (void)input;
    (void)length;
    (void)output;
    return false;
#endif
}

// The uncompressed size is not part of the zlib stream, so it is stored as a
// 4-byte little-endian prefix in front of the compressed data.
bool zlib_get_uncompressed_length(const char *input, size_t length,
                                  size_t *result) {
#if HAS_ZLIB
    if (length < sizeof(uint32_t)) {
        return false;
    }
    *result = decode_fixed_32(input);
    return true;
#else
    (void)input;
    (void)length;
    (void)result;
    return false;
#endif
}

bool zlib_compress(const char *input, size_t length, std::string *output,
                   int level) {
#if HAS_ZLIB
    uLongf bound = compressBound(static_cast<uLong>(length));
    output->resize(sizeof(uint32_t) + bound);
    encode_fixed_32(&(*output)[0], static_cast<uint32_t>(length));

    uLongf outlen = bound;
    int ret = compress2(reinterpret_cast<Bytef *>(&(*output)[sizeof(uint32_t)]),
                        &outlen, reinterpret_cast<const Bytef *>(input),
                        static_cast<uLong>(length), level);
    if (ret != Z_OK) {
        return false;
    }
    output->resize(sizeof(uint32_t) + outlen);
    return true;
#else
    (void)input;
    (void)length;
    (void)output;
    (void)level;
    return false;
#endif
}

bool zlib_uncompress(const char *input, size_t length, char *output) {
#if HAS_ZLIB
    size_t outlen = 0;
    if (!zlib_get_uncompressed_length(input, length, &outlen)) {
        return false;
    }
    uLongf dst_len = static_cast<uLongf>(outlen);
    int ret =
        uncompress(reinterpret_cast<Bytef *>(output), &dst_len,
                   reinterpret_cast<const Bytef *>(input + sizeof(uint32_t)),
                   static_cast<uLong>(length - sizeof(uint32_t)));
    if (ret != Z_OK) {
        return false;
    }
    return true;
#else
    (void)input;
    (void)length;
    (void)output;
    return false;
#endif
}

} // namespace

Compressor new_snappy_compressor() {
    return Compressor{&snappy_compress, &snappy_uncompress,
                      &get_snappy_uncompressed_length};
}

Compressor new_zstd_compressor() {
    return Compressor{&zstd_compress, &zstd_uncompress,
                      &get_zstd_uncompressed_length};
}

Compressor new_lz4_compressor() {
    return Compressor{&lz4_compress, &lz4_uncompress,
                      &lz4_get_uncompressed_length};
}

Compressor new_zlib_compressor() {
    return Compressor{&zlib_compress, &zlib_uncompress,
                      &zlib_get_uncompressed_length};
}

Compressor new_compressor(CompressionType type) {
    switch (type) {
    case CompressionType::Snappy:
        return new_snappy_compressor();
    case CompressionType::Zstd:
        return new_zstd_compressor();
    case CompressionType::Lz4:
        return new_lz4_compressor();
    case CompressionType::Zlib:
        return new_zlib_compressor();
    case CompressionType::None:
    default:
        return Compressor{};
    }
}

} // namespace anchordb
