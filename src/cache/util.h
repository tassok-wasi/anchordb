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

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

const int kNumShardBits = 4;
const int kNumShards = 1 << kNumShardBits;

// A key paired with its precomputed 64-bit hash, passed by value throughout the
// cache maps and lists.
struct HashedKey {
    std::string_view key;
    uint64_t hash;
};

struct LRUHandle {
    void *value;
    void (*deleter)(const std::string_view, void *value);

    uint32_t refs;
    bool in_cache; // True if referenced in map / retrievable
    size_t charge;
    uint64_t hash;

    size_t key_len;
    char key_data[1];

    std::string_view key() const { return std::string_view(key_data, key_len); }
};

struct LIRSHandle {
    void *value;
    void (*deleter)(std::string_view, void *value);

    uint32_t refs;
    bool in_cache;    // True if referenced in map / retrievable
    bool is_resident; // True if holds value & consumes charge
    bool is_lir;      // True = LIR block, False = HIR block
    bool in_stack;    // True if in S stack
    bool in_hir_list; // True if in Q list (resident HIRs)

    size_t charge;
    uint64_t hash;

    size_t key_len;
    char key_data[1];

    std::string_view key() const { return std::string_view(key_data, key_len); }
};

struct ClockProHandle {
    ClockProHandle *prev{nullptr};
    ClockProHandle *next{nullptr};

    void *value{nullptr};
    void (*deleter)(const std::string_view, void *value){nullptr};

    uint32_t refs{0};
    bool in_cache{false};    // True if indexed in hash map
    bool is_resident{false}; // True if holding allocated payload
    bool is_hot{false};      // True = Hot item, False = Cold item
    bool ref_bit{false};     // Second-chance bit

    size_t charge{0};
    uint64_t hash{0};

    size_t key_len{0};
    char key_data[1];

    std::string_view key() const { return std::string_view(key_data, key_len); }
};

struct ARCHandle {
    void *value;
    void (*deleter)(const std::string_view, void *value);

    uint32_t refs;
    bool in_cache; // true = in T1/T2 with value loaded; false = ghost entry
                   // (B1/B2)
    size_t charge;
    uint64_t hash;

    // Which ARC list the handle currently belongs to (T1/T2 resident, B1/B2
    // ghost).
    enum class State : uint8_t { kT1, kT2, kB1, kB2 };
    State state{State::kT1};

    size_t key_len;
    char key_data[1];

    std::string_view key() const { return std::string_view(key_data, key_len); }
};

} // namespace ANCHORDB_NAMESPACE
