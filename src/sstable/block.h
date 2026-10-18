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
#include "anchordb/iterator.h"
#include <cstddef>
#include <cstdint>

namespace anchordb {

struct BlockContents;

class Block {
  public:
    explicit Block(const BlockContents &contents);

    Block(const Block &) = delete;
    Block &operator=(const Block &) = delete;

    ~Block();

    size_t size() const { return size_; }

    Iterator *new_iterator(const Comparator comparator);

  private:
    class Iter;

    uint32_t num_restarts() const;

    const char *data_;
    size_t size_;
    uint32_t restart_offset_;
    bool owned_; // Block owns data_[]
};

} // namespace anchordb
