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

#include <anchordb/comparator.h>
#include <anchordb/slice.h>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
namespace anchordb {

// Distinguishes a live key-value entry from a tombstone (deletion marker).
// Encoded as the low byte of the 8-byte trailer in an internal key.
enum ValueType { TypeValue = 0x1, TypeDeletion = 0x0 };

// A monotonically increasing global sequence number assigned to every write.
using SeqNum = uint64_t;

// Maximum sequence number that fits in 56 bits (upper 8 bits are reserved for
// ValueType).
static const SeqNum max_seq_num = ((0x1ul << 56) - 1);

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
    Slice encode() {
        assert(!key_.empty());
        return Slice(key_);
    }

    Slice user_key() const { return extract_user_key(Slice(key_)); }

    void set_from(const ParsedInternalKey &p) {
        key_.clear();
        append_internal_key(&key_, p);
    }

    void clear() { key_.clear(); }

    // Returns a human-readable representation for debugging.
    std::string debug_string() const;
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
class InternalKeyComparator : public Comparator {
  private:
    const Comparator *user_comparator_;

  public:
    explicit InternalKeyComparator(const Comparator *c) : user_comparator_(c) {}
    const char *name() const override;
    int compare(const Slice &a, const Slice &b) const override;

    const Comparator *user_comparator() const { return user_comparator_; }
    int compare(const InternalKey &a, const InternalKey &b) const;
};

} // namespace anchordb
