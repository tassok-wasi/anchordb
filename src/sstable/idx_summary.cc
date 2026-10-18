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
#include "sstable/empty_iter.h"
#include "util/coding.h"
#include <cassert>
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

namespace {

// Iterates the parsed summary entries in ascending key order.  key() exposes
// the separator key and value() exposes the entry's absolute block offset as an
// 8-byte little-endian buffer (see SummaryIndexReader::new_iterator).
class SummaryIndexIter : public Iterator {
  public:
    SummaryIndexIter(const std::vector<SummaryEntry> &entries,
                     Comparator comparator)
        : entries_(entries), comparator_(comparator), index_(entries.size()) {
        set_current();
    }

    bool valid() const override { return index_ < entries_.size(); }

    void seek_to_first() override {
        index_ = entries_.empty() ? entries_.size() : 0;
        set_current();
    }

    void seek_to_last() override {
        index_ = entries_.empty() ? entries_.size() : entries_.size() - 1;
        set_current();
    }

    void seek(std::string_view target) override {
        // First entry whose key is >= target (lower_bound).
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
        index_ = lo;
        set_current();
    }

    void next() override {
        assert(valid());
        ++index_;
        set_current();
    }

    void prev() override {
        assert(valid());
        // Moving before the first entry makes the iterator invalid.
        index_ = (index_ == 0) ? entries_.size() : index_ - 1;
        set_current();
    }

    std::string_view key() const override {
        assert(valid());
        return key_;
    }

    std::string_view value() const override {
        assert(valid());
        return value_;
    }

    Status status() const override { return Status::ok(); }

  private:
    void set_current() {
        if (index_ < entries_.size()) {
            key_ = entries_[index_].key;
            encode_fixed_64(offset_buf_, entries_[index_].block_offset);
            value_ = std::string_view(offset_buf_, sizeof(uint64_t));
        } else {
            key_ = std::string_view();
            value_ = std::string_view();
        }
    }

    const std::vector<SummaryEntry> &entries_;
    const Comparator comparator_;
    size_t index_;
    std::string_view key_;
    std::string_view value_;
    // Backing storage for value_ (the encoded block offset of the current
    // entry).  Stable because the iterator is neither copyable nor movable.
    char offset_buf_[sizeof(uint64_t)]{};
};

} // namespace

Iterator *SummaryIndexReader::new_iterator() const {
    if (!valid_) {
        return new_error_iterator(
            Status::corruption("bad summary index contents"));
    }
    if (entries_.empty()) { return new_empty_iterator(); }
    return new SummaryIndexIter(entries_, comparator_);
}

} // namespace ANCHORDB_NAMESPACE
