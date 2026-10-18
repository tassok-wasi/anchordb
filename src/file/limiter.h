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

// Out-of-line definitions for the abstract Env/IO base classes.  These
// destructors are declared in the public env.h but were never defined, so any
// program that installs a concrete Env (e.g. PosixEnv) fails to link without
// them.  Defining them here also anchors each class's vtable.

#pragma once

#include "anchordb/anchordb_namespace.h"
#include <atomic>

namespace ANCHORDB_NAMESPACE {
// Concurrency limiter bounding the number of open file descriptors.
// Thread-safe.
class Limiter {
  public:
    explicit Limiter(int max_acquires) : allowed_(max_acquires) {}

    Limiter(const Limiter &) = delete;
    Limiter operator=(const Limiter &) = delete;

    // Returns true if a slot was acquired; caller must call release().
    bool acquire() noexcept {
        if (allowed_.load(std::memory_order_relaxed) <= 0) return false;
        int current = allowed_.fetch_sub(1, std::memory_order_relaxed);
        if (current <= 0) {
            allowed_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        return true;
    }

    void release() noexcept {
        allowed_.fetch_add(1, std::memory_order_relaxed);
    }

  private:
    // Positive = free slots; non-positive = number of waiters.
    std::atomic<int> allowed_;
};
} // namespace ANCHORDB_NAMESPACE
