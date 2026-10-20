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

#include "sstable/idx_summary.h"
#include "util/coding.h"
#include <cstdint>
#include <string_view>
#include <vector>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

namespace {
int ByteWiseCompare(std::string_view a, std::string_view b) {
    return a.compare(b);
}

// Fallback used when the caller supplies no comparator.
const Comparator kByteWiseComparator{&ByteWiseCompare, nullptr, nullptr,
                                     nullptr};
} // namespace

SummaryIndexWriter::SummaryIndexWriter(uint32_t block_size)
    : block_size_(block_size) {
    write_header_();
}

void SummaryIndexWriter::write_header_() {
    char header_buf[kSummaryBlockSizeSize];
    encode_fixed_32(header_buf, block_size_);
    buffer_.append(header_buf, kSummaryBlockSizeSize);
}

bool SummaryIndexWriter::add(std::string_view key, uint64_t block_offset,
                             uint32_t compressed_size) {
    if (key.size() > UINT32_MAX) { return false; }

    char length_buf[kSummaryKeyLenSize];
    encode_fixed_32(length_buf, static_cast<uint32_t>(key.size()));
    buffer_.append(length_buf, kSummaryKeyLenSize);

    buffer_.append(key.data(), key.size());

    char offset_buf[kSummaryOffsetSize];
    encode_fixed_64(offset_buf, block_offset);
    buffer_.append(offset_buf, kSummaryOffsetSize);

    char compressed_size_buf[kSummaryCompressedSizeSize];
    encode_fixed_32(compressed_size_buf, compressed_size);
    buffer_.append(compressed_size_buf, kSummaryCompressedSizeSize);

    ++entry_count_;
    return true;
}

SummaryIndexReader::SummaryIndexReader(std::string_view buffer,
                                       Comparator comparator)
    : comparator_(comparator), block_size_(0), valid_(true) {
    if (comparator_.compare == nullptr) { comparator_ = kByteWiseComparator; }

    const char *p = buffer.data();
    const char *limit = p + buffer.size();

    // The buffer must start with the block_size header; a zero block size is
    // meaningless and marks the summary malformed.
    if (static_cast<size_t>(limit - p) < kSummaryBlockSizeSize) {
        valid_ = false;
        return;
    }
    block_size_ = decode_fixed_32(p);
    p += kSummaryBlockSizeSize;
    if (block_size_ == 0) {
        valid_ = false;
        return;
    }

    while (p < limit) {
        if (static_cast<size_t>(limit - p) < kSummaryKeyLenSize) {
            valid_ = false;
            break;
        }
        const uint32_t key_len = decode_fixed_32(p);
        p += kSummaryKeyLenSize;

        if (static_cast<size_t>(limit - p) < static_cast<size_t>(key_len) +
                                                 kSummaryOffsetSize +
                                                 kSummaryCompressedSizeSize) {
            valid_ = false;
            break;
        }

        SummaryEntry entry;
        entry.key = std::string_view(p, key_len);
        p += key_len;
        entry.block_offset = decode_fixed_64(p);
        p += kSummaryOffsetSize;
        entry.compressed_size = decode_fixed_32(p);
        p += kSummaryCompressedSizeSize;

        entries_.push_back(entry);
    }

    if (!valid_) { entries_.clear(); }
}

bool SummaryIndexReader::seek(std::string_view target,
                              uint64_t *block_offset) const {
    return seek(target, block_offset, nullptr);
}

bool SummaryIndexReader::seek(std::string_view target, uint64_t *block_offset,
                              uint32_t *compressed_size) const {
    if (!valid_ || entries_.empty()) { return false; }

    const size_t lo = lower_bound(target);

    if (lo == entries_.size()) {
        // target is greater than every stored key.
        return false;
    }

    if (block_offset != nullptr) { *block_offset = entries_[lo].block_offset; }
    if (compressed_size != nullptr) {
        *compressed_size = entries_[lo].compressed_size;
    }
    return true;
}

size_t SummaryIndexReader::lower_bound(std::string_view target) const {
    if (!valid_) { return entries_.size(); }

    // First key >= target.
    size_t lo = 0;
    size_t hi = entries_.size();
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (comparator_.compare(entries_[mid].key, target) < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

} // namespace ANCHORDB_NAMESPACE
