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
#include <cstddef>
#include <cstdint>

namespace anchordb {

uint32_t murmurhash2_32(const char *data, size_t n, uint32_t seed);
uint64_t xx_hash64(const Slice &slice, uint64_t seed = 0);
uint64_t murmurhash3_64(const Slice &slice,
                        uint64_t seed = 0x9e3779b97f4a7c13ULL);

} // namespace anchordb
