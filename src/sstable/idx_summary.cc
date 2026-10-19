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

const Comparator kByteWiseComparator{&ByteWiseCompare, nullptr, nullptr,
                                     nullptr};
} // namespace

bool SummaryIndexWriter::add(std::string_view key, uint64_t block_offset) {
    if (key.size() > UINT16_MAX) { return false; }

    char length_buf[kSummaryKeyLenSize];
    encode_fixed_16(length_buf, static_cast<uint16_t>(key.size()));
    buffer_.append(length_buf, kSummaryKeyLenSize);

    buffer_.append(key.data(), key.size());

    char offset_buf[kSummaryOffsetSize];
    encode_fixed_64(offset_buf, block_offset);
    buffer_.append(offset_buf, kSummaryOffsetSize);

    ++entry_count_;
    return true;
}

SummaryIndexReader::SummaryIndexReader(std::string_view buffer,
                                       Comparator comparator)
    : comparator_(comparator), valid_(true) {
    if (comparator_.compare == nullptr) { comparator_ = kByteWiseComparator; }

    const char *p = buffer.data();
    const char *limit = p + buffer.size();

    while (p < limit) {
        if (static_cast<size_t>(limit - p) < kSummaryKeyLenSize) {
            valid_ = false;
            break;
        }
        const uint16_t key_len = decode_fixed_16(p);
        p += kSummaryKeyLenSize;

        if (static_cast<size_t>(limit - p) <
            static_cast<size_t>(key_len) + kSummaryOffsetSize) {
            valid_ = false;
            break;
        }

        SummaryEntry entry;
        entry.key = std::string_view(p, key_len);
        p += key_len;
        entry.block_offset = decode_fixed_64(p);
        p += kSummaryOffsetSize;

        entries_.push_back(entry);
    }

    if (!valid_) { entries_.clear(); }
}

bool SummaryIndexReader::seek(std::string_view target,
                              uint64_t *block_offset) const {
    if (!valid_ || entries_.empty()) { return false; }

    // lower_bound: first entry whose key is not less than target, i.e. the
    // first key >= target.
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

    if (lo == entries_.size()) {
        // target is greater than every stored key.
        return false;
    }

    if (block_offset != nullptr) { *block_offset = entries_[lo].block_offset; }
    return true;
}

} // namespace ANCHORDB_NAMESPACE
