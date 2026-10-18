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

#include "anchordb/comparator.h"
#include "util/coding.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

// Distinguishes a live key-value entry from a tombstone (deletion marker).
// Encoded as the low byte of the 8-byte trailer in an internal key.
enum class ValueType : uint8_t { value = 0x1, deletion = 0x0 };

enum class ValueFlag : uint8_t { inline_ = 0x1, blob = 0x0 };

// A monotonically increasing global sequence number assigned to every write.
using seq_num = uint64_t;

// Maximum sequence number that fits in 56 bits (upper 8 bits are reserved for
// ValueType).
static const seq_num k_max_seq_num = ((0x1ul << 56) - 1);

// A fully decomposed internal key.  The on-disk / in-memory internal key format
// is:
//   | user_key (variable) | sequence (7 bytes) | type (1 byte) |
// for a total of user_key.size() + 8 bytes.
struct ParsedInternalKey {
    std::string_view user_key;
    seq_num sequence;
    ValueType type;

    ParsedInternalKey() {}
    ParsedInternalKey(const std::string_view u, const seq_num seq, ValueType t)
        : user_key(u), sequence(seq), type(t) {}
};

namespace {
// Packs a 56-bit sequence number and a 1-byte ValueType into a single uint64_t.
// Layout: | sequence (56 bits) | type (8 bits) |
uint64_t pack_seq_and_type(uint64_t seq, ValueType t) {
    assert(seq <= k_max_seq_num);
    assert(t <= ValueType::value);
    return (seq << 8) | static_cast<uint64_t>(t);
}
} // namespace

// Returns the on-disk size of an internal key: user_key.size() + 8.
inline size_t internal_key_encoding_len(const ParsedInternalKey &key) {
    return key.user_key.size() + 8;
}

// Appends the encoded internal key (user_key + 8-byte trailer) to *result.
inline void append_internal_key(std::string *result,
                                const ParsedInternalKey &key) {
    result->append(key.user_key.data(), key.user_key.size());
    put_fixed_64(result, pack_seq_and_type(key.sequence, key.type));
}

// Decodes an internal key from a std::string_view.  Returns false if the
// std::string_view is too short or the ValueType byte is invalid.
bool parse_internal_key(std::string_view internal_key,
                        ParsedInternalKey *result);

// Strips the trailing 8 bytes (seq+type) and returns only the user_key portion.
inline std::string_view extract_user_key(std::string_view internal_key) {
    assert(internal_key.size() >= 8);
    return std::string_view(internal_key.data(), internal_key.size() - 8);
}

// An owned, encoded internal key.  Wraps a std::string that holds the full
// internal key encoding (user_key + 8-byte trailer).  Use this when the key
// must outlive the caller's stack frame.
class InternalKey {
  private:
    std::string key_;

  public:
    InternalKey() {}
    InternalKey(std::string_view user_key, seq_num s, ValueType t) {
        append_internal_key(&key_, ParsedInternalKey(user_key, s, t));
    }

    // Copies the raw bytes from s into the internal buffer.
    bool decode_from(std::string_view s) {
        key_.assign(s.data(), s.size());
        return !key_.empty();
    }

    // Returns the encoded internal key as a std::string_view.  Must not be
    // empty.
    std::string_view encode() const {
        assert(!key_.empty());
        return std::string_view(key_);
    }

    std::string_view user_key() const {
        return extract_user_key(std::string_view(key_));
    }

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
    LookUpKey(std::string_view user_key, seq_num sequence);

    LookUpKey(const LookUpKey &) = delete;
    LookUpKey &operator=(const LookUpKey &) = delete;

    ~LookUpKey() {
        if (start_ != space_)
            delete[] start_;
    }

    // Returns the full memtable key including the varint32 length prefix.
    std::string_view memtable_key() const {
        return std::string_view(start_, end_ - start_);
    }
    // Returns the internal key portion (user_key + 8-byte trailer).
    std::string_view internal_key() const {
        return std::string_view(kstart_, end_ - kstart_);
    }
    // Returns just the user_key portion.
    std::string_view user_key() const {
        return std::string_view(kstart_, end_ - kstart_ - 8);
    }

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
class InternalKeyComparator {
  public:
    explicit InternalKeyComparator(const Comparator *c) : user_comparator_(c) {}
    const char *name() const { return "anchordb.InternalKeyComparator"; }

    int compare(std::string_view a, std::string_view b) const {
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

    const Comparator *user_comparator() const { return user_comparator_; }

    int compare(const InternalKey &a, const InternalKey &b) const {
        return compare(a.encode(), b.encode());
    }

    void find_shortest_separator(std::string *start, std::string_view limit) {
        // Attempt to shorten the user portion of the key
        std::string_view user_start = extract_user_key(*start);
        std::string_view user_limit = extract_user_key(limit);
        std::string tmp(user_start.data(), user_start.size());
        user_comparator_->find_shortest_separator(&tmp, user_limit);
        if (tmp.size() < user_start.size() &&
            user_comparator_->compare(user_start, tmp) < 0) {
            // User key has become shorter physically, but larger logically.
            // Tack on the earliest possible number to the shortened user key.
            put_fixed_64(&tmp,
                         pack_seq_and_type(k_max_seq_num, ValueType::value));
            assert(this->compare(*start, tmp) < 0);
            assert(this->compare(tmp, limit) < 0);
            start->swap(tmp);
        }
    }
    void find_short_successor(std::string *key) const {
        std::string_view user_key = extract_user_key(*key);
        std::string tmp(user_key.data(), user_key.size());
        user_comparator_->find_short_successor(&tmp);
        if (tmp.size() < user_key.size() &&
            user_comparator_->compare(user_key, tmp) < 0) {
            // User key has become shorter physically, but larger logically.
            // Tack on the earliest possible number to the shortened user key.
            put_fixed_64(&tmp,
                         pack_seq_and_type(k_max_seq_num, ValueType::value));
            assert(this->compare(*key, tmp) < 0);
            key->swap(tmp);
        }
    }

  private:
    const Comparator *user_comparator_;
};

struct BlobHandle {
    uint64_t offset;
    uint32_t file_id;
    uint32_t size;

    BlobHandle() {}
    BlobHandle(uint64_t offset, uint32_t file_id, uint32_t size)
        : offset(offset), file_id(file_id), size(size) {}
};

inline void encode_blob_handle(BlobHandle *handle, std::string *result) {
    put_varint_64(result, handle->offset);
    put_varint_32(result, handle->file_id);
    put_varint_32(result, handle->size);
}

inline bool parse_blob_handle(std::string_view *blob_handle,
                              BlobHandle *result) {
    return get_varint_64(blob_handle, &result->offset) &&
           get_varint_32(blob_handle, &result->file_id) &&
           get_varint_32(blob_handle, &result->size);
}

} // namespace anchordb
