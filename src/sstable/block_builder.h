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

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

struct Options;

class BlockBuilder {
  public:
      explicit BlockBuilder(const Options *options);

    BlockBuilder(const BlockBuilder &) = delete;
    BlockBuilder &operator=(const BlockBuilder &) = delete;

    void reset();

    void add(std::string_view key, std::string_view value);

    std::string_view finish();

    size_t current_size_estimate() const;

    bool empty() const { return buffer_.empty(); }

  private:
    const Options *options_;
    std::string buffer_;             // Destination buffer
    std::vector<uint32_t> restarts_; // Restart points
    int counter_;                    // Number of entriess emitted since restart
    bool finished_;                  // Has finish() been called?
    std::string last_key_;
};

} // namespace anchordb
