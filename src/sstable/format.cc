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

#include "anchordb/compressor.h"
#include "anchordb/env.h"
#include "anchordb/options.h"
#include "anchordb/status.h"
#include "format.h"
#include "util/coding.h"
#include "util/crc32.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace anchordb {

void BlockHandle::encode_to(std::string *dst) const {
    // Sanity check that all fields have been set
    assert(offset_ != ~static_cast<uint64_t>(0));
    assert(size_ != ~static_cast<uint64_t>(0));
    put_varint_64(dst, offset_);
    put_varint_64(dst, size_);
}

Status BlockHandle::decode_from(std::string_view *input) {
    if (get_varint_64(input, &offset_) && get_varint_64(input, &size_)) {
        return Status::ok();
    } else {
        return Status::corruption("bad block handle");
    }
}

void Footer::encode_to(std::string *dst) const {
    size_t original_size = dst->size();
    metaindex_handle_.encode_to(dst);
    index_handle_.encode_to(dst);
    dst->resize(2 * BlockHandle::kMaxEncodedLength); // Padding
    put_fixed_32(dst, static_cast<uint32_t>(kSSTableMagicNumber & 0xffffffffu));
    put_fixed_32(dst, static_cast<uint32_t>(kSSTableMagicNumber >> 32));
    assert(dst->size() == original_size + kEncodedLength);
    (void)original_size; // Disable unused variable warning.
}

Status Footer::decode_from(std::string_view *input) {
    if (input->size() < kEncodedLength) {
        Status::corruption("not an sstable (footer too short)");
    }

    const char *magic_ptr = input->data() + kEncodedLength - 8;
    const uint32_t magic_lo = decode_fixed_32(magic_ptr);
    const uint32_t magic_hi = decode_fixed_32(magic_ptr + 4);
    const uint64_t magic = ((static_cast<uint64_t>(magic_hi) << 32) |
                            static_cast<uint64_t>(magic_lo));
    if (magic != kSSTableMagicNumber) {
        return Status::corruption("not an sstable (bad magic number)");
    }

    Status result = metaindex_handle_.decode_from(input);
    if (result.is_ok()) {
        result = index_handle_.decode_from(input);
    }
    if (result.is_ok()) {
        const char *end = magic_ptr + 8;
        *input = std::string_view(end, input->data() + input->size() - end);
    }
    return result;
}

Status read_block(RandomAccessFileReader *file, const ReadOptions &options,
                  const BlockHandle &handle, BlockContents *result) {
    result->data = std::string_view();
    result->cachable = false;
    result->heap_allocated = false;

    // Read the block contents as well as the type/crc footer.
    // See table_builder.cc for the code that built this structure.
    size_t n = static_cast<size_t>(handle.size());
    char *buf = new char[n + kBlockTrailerSize];
    std::string_view contents;
    Status s =
        file->read(handle.offset(), n + kBlockTrailerSize, &contents, buf);
    if (!s.is_ok()) {
        delete[] buf;
        return s;
    }
    if (contents.size() != n + kBlockTrailerSize) {
        delete[] buf;
        return Status::corruption("truncated block read");
    }

    // Check the crc of the type and the block contents
    const char *data = contents.data(); // Pointer to where Read put the data
    if (options.verify_checksums) {
        const uint32_t crc = unmask(decode_fixed_32(data + n + 1));
        const uint32_t actual = value(data, n + 1);
        if (actual != crc) {
            delete[] buf;
            s = Status::corruption("block checksum mismatch");
            return s;
        }
    }

    switch (data[n]) {
    case static_cast<int>(CompressionType::None):
        if (data != buf) {
            delete[] buf;
            result->data = std::string_view(data, n);
            result->heap_allocated = false;
            result->cachable = false; // Do not double-cache
        } else {
            result->data = std::string_view(buf, n);
            result->heap_allocated = true;
            result->cachable = true;
        }

        break;
    case static_cast<int>(CompressionType::Snappy): {
        Compressor snappy_compressor = new_snappy_compressor();
        size_t ulength = 0;
        if (!snappy_compressor.get_uncompressed_length(data, n, &ulength)) {
            delete[] buf;
            return Status::corruption(
                "corrupted snappy compressed block length");
        }
        char *ubuf = new char[ulength];
        if (!snappy_compressor.uncompress(data, n, ubuf)) {
            delete[] buf;
            delete[] ubuf;
            return Status::corruption(
                "corrupted snappy compressed block contents");
        }
        delete[] buf;
        result->data = std::string_view(ubuf, ulength);
        result->heap_allocated = true;
        result->cachable = true;
        break;
    }
    case static_cast<int>(CompressionType::Zstd): {
        Compressor zstd_compressor = new_zstd_compressor();
        size_t ulength = 0;
        if (!zstd_compressor.get_uncompressed_length(data, n, &ulength)) {
            delete[] buf;
            return Status::corruption("corrupted zstd compressed block length");
        }
        char *ubuf = new char[ulength];
        if (!zstd_compressor.uncompress(data, n, ubuf)) {
            delete[] buf;
            delete[] ubuf;
            return Status::corruption(
                "corrupted zstd compressed block contents");
        }
        delete[] buf;
        result->data = std::string_view(ubuf, ulength);
        result->heap_allocated = true;
        result->cachable = true;
        break;
    }
    default:
        delete[] buf;
        return Status::corruption("bad block type");
    }

    return Status::ok();
}

} // namespace anchordb
