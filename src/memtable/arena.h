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
#include <vector>

namespace anchordb {

class Arena {
  public:
    explicit Arena();

    ~Arena();

    Arena(const Arena &) = delete;
    Arena &operator=(const Arena &) = delete;

    char *allocate(size_t bytes);
    char *allocate_aligned(size_t bytes);

    size_t memory_usage() const { return memory_usage_; }

  private:
    char *allocate_fallback(size_t bytes);
    char *allocate_new_block(size_t block_bytes);

    char *alloc_ptr_;
    size_t alloc_bytes_remaining_;
    std::vector<char *> blocks_;

    size_t memory_usage_;
};

} // namespace anchordb
