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

#include "sstable/empty_iter.h"
#include "sstable/idx_block_reader.h"
#include "util/coding.h"
#include <cassert>
#include <cstdint>
#include <string_view>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

namespace {
// Byte offsets within a forward slot (see idx_block_writer.h layout doc).
inline constexpr size_t kSlotTypeOff = 2;         // value_type (1B)
inline constexpr size_t kSlotFlagOff = 3;         // value_flag (1B)
inline constexpr size_t kSlotGlobalOffsetOff = 4; // data_global_offset (8B)

int ByteWiseCompare(std::string_view a, std::string_view b) {
    return a.compare(b);
}

const Comparator kByteWiseComparator{&ByteWiseCompare, nullptr, nullptr,
                                     nullptr};
} // namespace

IndexBlockReader::IndexBlockReader(std::string_view block)
    : data_(block.data()), size_(block.size()), slot_count_(0), valid_(false) {
    if (size_ < kIndexHeaderSize) { return; }

    slot_count_ = decode_fixed_16(data_);
    const size_t slots_end =
        kIndexHeaderSize + static_cast<size_t>(slot_count_) * kIndexSlotSize;
    if (slots_end > size_) { return; }

    // Validate every entry so callers can decode without bounds checks.
    for (size_t i = 0; i < slot_count_; ++i) {
        const char *slot = data_ + kIndexHeaderSize + i * kIndexSlotSize;
        const uint16_t key_off = decode_fixed_16(slot);

        // The payload must live after the forward slot array and must hold at
        // least its 2-byte length prefix.
        if (key_off < slots_end ||
            static_cast<size_t>(key_off) + sizeof(uint16_t) > size_) {
            return;
        }

        const uint16_t key_len = decode_fixed_16(data_ + key_off);
        const size_t key_start =
            static_cast<size_t>(key_off) + sizeof(uint16_t);
        if (key_start + key_len + sizeof(uint64_t) > size_) { return; }
    }

    valid_ = true;
}

IndexBlockEntry IndexBlockReader::entry(size_t index) const {
    assert(valid_);
    assert(index < slot_count_);

    const char *slot = data_ + kIndexHeaderSize + index * kIndexSlotSize;
    const uint16_t key_off = decode_fixed_16(slot);
    const char *payload = data_ + key_off;
    const uint16_t key_len = decode_fixed_16(payload);

    IndexBlockEntry result;
    result.key = std::string_view(payload + sizeof(uint16_t), key_len);
    result.handle.type =
        static_cast<ValueType>(static_cast<uint8_t>(slot[kSlotTypeOff]));
    result.handle.flag =
        static_cast<ValueFlag>(static_cast<uint8_t>(slot[kSlotFlagOff]));
    result.handle.offset = decode_fixed_64(slot + kSlotGlobalOffsetOff);
    result.timestamp = decode_fixed_64(payload + sizeof(uint16_t) + key_len);
    return result;
}

bool IndexBlockReader::decode_value(std::string_view value, ValueHandle *handle,
                                    uint64_t *timestamp) {
    if (value.size() != kIndexValueSize) { return false; }

    const char *p = value.data();
    if (handle != nullptr) {
        handle->type = static_cast<ValueType>(static_cast<uint8_t>(p[0]));
        handle->flag = static_cast<ValueFlag>(static_cast<uint8_t>(p[1]));
        handle->offset = decode_fixed_64(p + 2);
    }
    if (timestamp != nullptr) {
        *timestamp = decode_fixed_64(p + 2 + sizeof(uint64_t));
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
            encode_fixed_64(value_buf_ + 2, current.handle.offset);
            encode_fixed_64(value_buf_ + 2 + sizeof(uint64_t),
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
    // Backing storage for value_; stable because the iterator is neither
    // copyable nor movable.
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
