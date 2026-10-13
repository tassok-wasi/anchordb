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

#include "anchordb/export.h"
#include "anchordb/status.h"

namespace anchordb {

struct ANCHORDB_EXPORT Iterator {
    void *state = nullptr;

    bool (*valid)(const void *state) = nullptr;
    void (*next)(void *state) = nullptr;
    void (*prev)(void *state) = nullptr;
    void (*seek)(void *state, std::string_view key) = nullptr;
    void (*seek_to_first)(void *state) = nullptr;
    void (*seek_to_last)(void *state) = nullptr;
    std::string_view (*key)(const void *state) = nullptr;
    std::string_view (*value)(const void *state) = nullptr;
    Status (*status)(const void *state) = nullptr;

    void (*destroy)(void *state) = nullptr;

    Iterator() = default;

    Iterator(const Iterator &) = delete;
    Iterator &operator=(const Iterator &) = delete;

    Iterator(Iterator &&other) noexcept { *this = std::move(other); }
    Iterator &operator=(Iterator &&other) noexcept {
        if (this != &other) {
            state = other.state;
            valid = other.valid;
            next = other.next;
            prev = other.prev;
            seek = other.seek;
            seek_to_first = other.seek_to_first;
            seek_to_last = other.seek_to_last;
            key = other.key;
            value = other.value;
            status = other.status;
            destroy = other.destroy;

            other.state = nullptr;
            other.destroy = nullptr;
        }
        return *this;
    }

    /// Destroys the Iterator, releasing any associated resources.
    ~Iterator() {
        if (destroy && state) {
            destroy(state);
        }
    }
};

} // namespace anchordb
