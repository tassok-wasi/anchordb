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

#include "anchordb/slice.h"
#include "util/coding.h"
#include "util/comparator.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
namespace anchordb {

// Distinguishes a live key-value entry from a tombstone (deletion marker).
// Encoded as the low byte of the 8-byte trailer in an internal key.
enum ValueType { TypeValue = 0x1, TypeDeletion = 0x0 };

// A monotonically increasing global sequence number assigned to every write.
using SeqNum = uint64_t;

// Maximum sequence number that fits in 56 bits (upper 8 bits are reserved for
// ValueType).
static const SeqNum k_max_seq_num = ((0x1ul << 56) - 1);

// A fully decomposed internal key.  The on-disk / in-memory internal key format
// is:
//   | user_key (variable) | sequence (7 bytes) | type (1 byte) |
// for a total of user_key.size() + 8 bytes.
struct ParsedInternalKey {
    Slice user_key;
    SeqNum sequence;
    ValueType type;

    ParsedInternalKey() {}
    ParsedInternalKey(const Slice &u, const SeqNum seq, ValueType t)
        : user_key(u), sequence(seq), type(t) {}
};

namespace {
// Packs a 56-bit sequence number and a 1-byte ValueType into a single uint64_t.
// Layout: | sequence (56 bits) | type (8 bits) |
uint64_t pack_seq_and_type(uint64_t seq, ValueType t) {
    assert(seq <= k_max_seq_num);
    assert(t <= ValueType::TypeValue);
    return (seq << 8) | static_cast<uint64_t>(t);
}
} // namespace

// Returns the on-disk size of an internal key: user_key.size() + 8.
size_t internal_key_encoding_len(const ParsedInternalKey &key);

// Appends the encoded internal key (user_key + 8-byte trailer) to *result.
void append_internal_key(std::string *result, const ParsedInternalKey &key);

// Decodes an internal key from a Slice.  Returns false if the slice is too
// short or the ValueType byte is invalid.
bool parse_internal_key(const Slice &internal_key, ParsedInternalKey *result);

// Strips the trailing 8 bytes (seq+type) and returns only the user_key portion.
Slice extract_user_key(const Slice &internal_key);

// An owned, encoded internal key.  Wraps a std::string that holds the full
// internal key encoding (user_key + 8-byte trailer).  Use this when the key
// must outlive the caller's stack frame.
class InternalKey {
  private:
    std::string key_;

  public:
    InternalKey() {}
    InternalKey(const Slice &user_key, SeqNum s, ValueType t) {
        append_internal_key(&key_, ParsedInternalKey(user_key, s, t));
    }

    // Copies the raw bytes from s into the internal buffer.
    bool decode_from(const Slice &s) {
        key_.assign(s.data(), s.size());
        return !key_.empty();
    }

    // Returns the encoded internal key as a Slice.  Must not be empty.
    Slice encode() const {
        assert(!key_.empty());
        return Slice(key_);
    }

    Slice user_key() const { return extract_user_key(Slice(key_)); }

    void set_from(const ParsedInternalKey &p) {
        key_.clear();
        append_internal_key(&key_, p);
    }

    void clear() { key_.clear(); }
};

// A key used for memtable lookups.  Encodes the key in the memtable format:
//   | varint32(internal_key_size) | user_key | seq+type (8 bytes) |
//
// Uses a small-buffer optimization: keys up to ~187 bytes of user data fit in
// the inline `space_` array, avoiding a heap allocation.
class LookUpKey {
  public:
    // Constructs a memtable key for the given user_key and sequence number.
    // The value type is always TypeValue (used for point lookups).
    LookUpKey(const Slice &user_key, SeqNum sequence);

    LookUpKey(const LookUpKey &) = delete;
    LookUpKey &operator=(const LookUpKey &) = delete;

    ~LookUpKey() {
        if (start_ != space_)
            delete[] start_;
    }

    // Returns the full memtable key including the varint32 length prefix.
    Slice memtable_key() const { return Slice(start_, end_ - start_); }
    // Returns the internal key portion (user_key + 8-byte trailer).
    Slice internal_key() const { return Slice(kstart_, end_ - kstart_); }
    // Returns just the user_key portion.
    Slice user_key() const { return Slice(kstart_, end_ - kstart_ - 8); }

  private:
    const char *start_;  // beginning of the memtable key (varint32 prefix)
    const char *kstart_; // beginning of the internal key (user_key)
    const char *end_;    // one past the end of the encoded key
    char space_[200];    // inline storage for small-buffer optimization
};

// Comparator for internal keys.  Orders first by user_key (ascending, via the
// user-supplied comparator), then by sequence number (descending) so that
// newer versions of the same user_key appear first.  This is the ordering
// used by the memtable and SSTables.
template <typename UserComparator = ByteWiseComparator>
class InternalKeyComparator {
  public:
    explicit InternalKeyComparator(const UserComparator *c)
        : user_comparator_(c) {}
    const char *name() const { return "anchordb.InternalKeyComparator"; }

    int compare(const Slice &a, const Slice &b) const {
        int r =
            user_comparator_->compare(extract_user_key(a), extract_user_key(b));
        if (r == 0) {
            const uint64_t anum = decode_fixed_64(a.data() + a.size() - 8);
            const uint64_t bnum = decode_fixed_64(b.data() + b.size() - 8);
            if (anum > bnum) {
                return -1;
            } else if (anum < bnum) {
                return 1;
            }
        }
        return r;
    }

    const UserComparator *user_comparator() const { return user_comparator_; }

    int compare(const InternalKey &a, const InternalKey &b) const {
        return compare(a.encode(), b.encode());
    }

    void find_shortest_separator(std::string *start, const Slice &limit) {
        // Attempt to shorten the user portion of the key
        Slice user_start = extract_user_key(*start);
        Slice user_limit = extract_user_key(limit);
        std::string tmp(user_start.data(), user_start.size());
        user_comparator_->find_shortest_separator(&tmp, user_limit);
        if (tmp.size() < user_start.size() &&
            user_comparator_->compare(user_start, tmp) < 0) {
            // User key has become shorter physically, but larger logically.
            // Tack on the earliest possible number to the shortened user key.
            put_fixed_64(
                &tmp, pack_seq_and_type(k_max_seq_num, ValueType::TypeValue));
            assert(this->compare(*start, tmp) < 0);
            assert(this->compare(tmp, limit) < 0);
            start->swap(tmp);
        }
    }
    void find_short_successor(std::string *key) const {
        Slice user_key = extract_user_key(*key);
        std::string tmp(user_key.data(), user_key.size());
        user_comparator_->find_short_successor(&tmp);
        if (tmp.size() < user_key.size() &&
            user_comparator_->compare(user_key, tmp) < 0) {
            // User key has become shorter physically, but larger logically.
            // Tack on the earliest possible number to the shortened user key.
            put_fixed_64(
                &tmp, pack_seq_and_type(k_max_seq_num, ValueType::TypeValue));
            assert(this->compare(*key, tmp) < 0);
            key->swap(tmp);
        }
    }

  private:
    const UserComparator *user_comparator_;
};

struct alignas(8) ParsedBlobAddress {
    uint64_t offset;
    uint32_t file_id;
    uint32_t size;

    ParsedBlobAddress() {}
    ParsedBlobAddress(uint64_t offset, uint32_t file_id, uint32_t size)
        : offset(offset), file_id(file_id), size(size) {}
};

void append_blob_address(char *result, const ParsedBlobAddress &addr);

class BlobAddress {
  public:
    BlobAddress() {}
    BlobAddress(uint64_t offset, uint32_t file_id, uint32_t size) {
        append_blob_address(addr_, ParsedBlobAddress(offset, file_id, size));
    }

    Slice encode() const { return Slice(addr_, 16); }

    void set_from(const ParsedBlobAddress &addr) {
        memset(addr_, 0, 16);
        append_blob_address(addr_, addr);
    }

    void clear() { memset(addr_, 0, 16); }

  private:
    char addr_[16];
};

} // namespace anchordb
