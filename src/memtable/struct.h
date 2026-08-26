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

#include <anchordb/slice.h>
#include <cstdint>
namespace anchordb {

enum class OpType : uint8_t { PUT = 0, DELETE = 1 };

struct KeyType {
    const Slice key;
    uint32_t seq_num;
    OpType op_type;
    uint64_t tstamp;
};

} // namespace anchordb
