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

#include "sstable/idx_block_reader.h"
#include "sstable/empty_iter.h"
#include "util/coding.h"
#include <cassert>
#include <cstdint>
#include <string_view>

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

IndexBlockReader::IndexBlockReader(std::string_view block, size_t block_size)
    : data_(block.data()), size_(block.size()),
      block_size_(round_index_block_size(block_size)), slot_count_(0),
      valid_(false) {
    // A block is always exactly one configured (aligned) segment.  Only the
    // first block_size_ bytes are parsed; extra trailing bytes are ignored.
    if (size_ < kIndexHeaderSize || size_ < block_size_) { return; }

    slot_count_ = decode_fixed_32(data_);
    const uint32_t free_space_boundary =
        decode_fixed_32(data_ + sizeof(uint32_t));

    const size_t slots_end =
        kIndexHeaderSize + static_cast<size_t>(slot_count_) * kIndexSlotSize;
    if (slots_end > block_size_ || free_space_boundary > block_size_) {
        return;
    }

    // The backward payload must start at or after the forward slot array once
    // at least one entry has been stored.
    if (slot_count_ != 0 && free_space_boundary < slots_end) { return; }

    // Validate every entry so callers can decode without bounds checks.
    for (size_t i = 0; i < slot_count_; ++i) {
        const char *slot = data_ + kIndexHeaderSize + i * kIndexSlotSize;
        const uint32_t key_off = decode_fixed_32(slot);

        // Payloads grow down from the free-space boundary, so every payload
        // starts at or above it; it must hold at least its 4-byte length
        // prefix and stay inside the segment.
        if (key_off < free_space_boundary ||
            static_cast<size_t>(key_off) + sizeof(uint32_t) > block_size_) {
            return;
        }

        const uint32_t key_len = decode_fixed_32(data_ + key_off);
        const size_t key_start =
            static_cast<size_t>(key_off) + sizeof(uint32_t);
        if (key_start + key_len + sizeof(uint64_t) > block_size_) { return; }
    }

    valid_ = true;
}

IndexBlockEntry IndexBlockReader::entry(size_t index) const {
    assert(valid_);
    assert(index < slot_count_);

    const char *slot = data_ + kIndexHeaderSize + index * kIndexSlotSize;
    const uint32_t key_off = decode_fixed_32(slot);
    const char *payload = data_ + key_off;
    const uint32_t key_len = decode_fixed_32(payload);

    IndexBlockEntry result;
    result.key = std::string_view(payload + sizeof(uint32_t), key_len);
    result.handle.type =
        static_cast<ValueType>(static_cast<uint8_t>(slot[kSlotTypeOff]));
    result.handle.flag =
        static_cast<ValueFlag>(static_cast<uint8_t>(slot[kSlotFlagOff]));
    result.handle.data_size = decode_fixed_32(slot + kSlotDataSizeOff);
    result.handle.offset = decode_fixed_64(slot + kSlotGlobalOffsetOff);
    result.timestamp = decode_fixed_64(payload + sizeof(uint32_t) + key_len);
    return result;
}

bool IndexBlockReader::decode_value(std::string_view value, ValueHandle *handle,
                                    uint64_t *timestamp) {
    if (value.size() != kIndexValueSize) { return false; }

    const char *p = value.data();
    if (handle != nullptr) {
        handle->type = static_cast<ValueType>(static_cast<uint8_t>(p[0]));
        handle->flag = static_cast<ValueFlag>(static_cast<uint8_t>(p[1]));
        handle->data_size = decode_fixed_32(p + 2);
        handle->offset = decode_fixed_64(p + 2 + sizeof(uint32_t));
    }
    if (timestamp != nullptr) {
        *timestamp =
            decode_fixed_64(p + 2 + sizeof(uint32_t) + sizeof(uint64_t));
    }
    return true;
}

namespace {

// Iterates the entries of an index block in ascending key order.  key() exposes
// the entry key and value() exposes the packed handle+timestamp record.
class IndexBlockIter : public Iterator {
  public:
    IndexBlockIter(const IndexBlockReader &reader, Comparator comparator)
        : reader_(reader), comparator_(comparator), index_(reader.size()) {
        set_current();
    }

    bool valid() const override { return index_ < reader_.size(); }

    void seek_to_first() override {
        index_ = reader_.empty() ? reader_.size() : 0;
        set_current();
    }

    void seek_to_last() override {
        index_ = reader_.empty() ? reader_.size() : reader_.size() - 1;
        set_current();
    }

    void seek(std::string_view target) override {
        // First entry whose key is >= target (lower_bound).
        size_t lo = 0;
        size_t hi = reader_.size();
        while (lo < hi) {
            const size_t mid = lo + (hi - lo) / 2;
            if (comparator_.compare(reader_.key(mid), target) < 0) {
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
        index_ = (index_ == 0) ? reader_.size() : index_ - 1;
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
        if (index_ < reader_.size()) {
            const IndexBlockEntry current = reader_.entry(index_);
            key_ = current.key;
            value_buf_[0] =
                static_cast<char>(static_cast<uint8_t>(current.handle.type));
            value_buf_[1] =
                static_cast<char>(static_cast<uint8_t>(current.handle.flag));
            encode_fixed_32(value_buf_ + 2, current.handle.data_size);
            encode_fixed_64(value_buf_ + 2 + sizeof(uint32_t),
                            current.handle.offset);
            encode_fixed_64(value_buf_ + 2 + sizeof(uint32_t) +
                                sizeof(uint64_t),
                            current.timestamp);
            value_ = std::string_view(value_buf_, kIndexValueSize);
        } else {
            key_ = std::string_view();
            value_ = std::string_view();
        }
    }

    const IndexBlockReader &reader_;
    const Comparator comparator_;
    size_t index_;
    std::string_view key_;
    std::string_view value_;
    // Backing storage for value_.  Stable because the iterator is not copyable
    // or movable.
    char value_buf_[kIndexValueSize]{};
};

} // namespace

Iterator *IndexBlockReader::new_iterator(Comparator comparator) const {
    if (!valid_) {
        return new_error_iterator(
            Status::corruption("bad index block contents"));
    }
    if (empty()) { return new_empty_iterator(); }
    if (comparator.compare == nullptr) { comparator = kByteWiseComparator; }
    return new IndexBlockIter(*this, comparator);
}

} // namespace ANCHORDB_NAMESPACE
