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

#include "compressor.h"

#include <cstddef>
#include <string>

#include "port/port.h" // IWYU pragma: keep

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

} // namespace

bool Compressor::compress(const char *input, size_t length, std::string *output,
                          CompressionType t, int level) {
    switch (t) {
    case CompressionType::Snappy: {
#if HAS_SNAPPY
        output->resize(snappy::MaxCompressedLength(length));
        size_t outlen = 0;
        snappy::RawCompress(input, length, &(*output)[0], &outlen);
        output->resize(outlen);
        return true;
#else
        break;
#endif
    }
    case CompressionType::Zstd: {
#if HAS_ZSTD
        size_t outlen = ZSTD_compressBound(length);
        output->resize(outlen);

        ZSTD_CCtx *ctx = ZSTD_createCCtx();
        if (ctx == nullptr) {
            return false;
        }
        ZSTD_compressionParameters parameters =
            ZSTD_getCParams(level, std::max(length, size_t{1}), /*dictSize=*/0);
        if (ZSTD_isError(ZSTD_CCtx_setCParams(ctx, parameters))) {
            ZSTD_freeCCtx(ctx);
            return false;
        }
        outlen =
            ZSTD_compress2(ctx, &(*output)[0], output->size(), input, length);
        ZSTD_freeCCtx(ctx);
        if (ZSTD_isError(outlen)) {
            return false;
        }
        output->resize(outlen);
        return true;
#else
        break;
#endif
    }
    case CompressionType::None:
        break;
    }

    // Silence compiler warnings about unused arguments and report that the
    // requested compression type is not available.
    (void)level;
    (void)input;
    (void)length;
    (void)output;
    return false;
}

bool Compressor::uncompress(const char *input, size_t length,
                            std::string *output, CompressionType t) {
    switch (t) {
    case CompressionType::Snappy: {
#if HAS_SNAPPY
        return snappy::RawUncompress(input, length, output);
#else
        break;
#endif
    }
    case CompressionType::Zstd: {
#if HAS_ZSTD
        size_t outlen = 0;
        if (!zstd_get_uncompressed_length(input, length, &outlen)) {
            return false;
        }
        output->resize(outlen);

        ZSTD_DCtx *ctx = ZSTD_createDCtx();
        if (ctx == nullptr) {
            return false;
        }
        size_t result =
            ZSTD_decompressDCtx(ctx, &(*output)[0], outlen, input, length);
        ZSTD_freeDCtx(ctx);
        if (ZSTD_isError(result)) {
            return false;
        }
        output->resize(result);
        return true;
#else
        break;
#endif
    }
    case CompressionType::None:
        break;
    }

    // Silence compiler warnings about unused arguments and report that the
    // requested compression type is not available.
    (void)input;
    (void)length;
    (void)output;
    return false;
}

bool Compressor::get_uncompressed_length(const char *input, size_t length,
                                         size_t *result, CompressionType t) {
    switch (t) {
    case CompressionType::Snappy: {
#if HAS_SNAPPY
        return snappy::GetUncompressedLength(input, length, result);
#else
        break;
#endif
    }
    case CompressionType::Zstd: {
#if HAS_ZSTD
        return zstd_get_uncompressed_length(input, length, result);
#else
        break;
#endif
    }
    case CompressionType::None:
        break;
    }

    // Silence compiler warnings about unused arguments and report that the
    // requested compression type is not available.
    (void)input;
    (void)length;
    (void)result;
    return false;
}

} // namespace anchordb
