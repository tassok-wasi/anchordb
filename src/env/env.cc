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

#include "anchordb/env.h"
#include "env/env_default.h"

#include <atomic>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

namespace {
std::atomic<Env *> g_default_env_override{nullptr};
} // namespace

Env *Env::default_env() {
    // An installed override wins; it is set/cleared with acquire/release so a
    // test can swap environments without data races.
    Env *override_env = g_default_env_override.load(std::memory_order_acquire);
    if (override_env != nullptr) { return override_env; }

    // Thread-safe function-local static (C++11); after the first call the fast
    // path is a single guard load and branch.
    //
    // Intentionally never freed: destroying it at static teardown would run
    // PosixEnv's destructor (tearing down the io_uring rings and thread pool)
    // while other statics may still use the Env.  The OS reclaims it at exit.
    static Env *const platform_env = env_internal::create_default_env();
    return platform_env;
}

void Env::set_default_env(Env *env) {
    g_default_env_override.store(env, std::memory_order_release);
}

Env::~Env() = default;
EnvWrapper::~EnvWrapper() = default;

SequentialFileReader::~SequentialFileReader() = default;
RandomAccessFileReader::~RandomAccessFileReader() = default;
WritableFileWriter::~WritableFileWriter() = default;

Logger::~Logger() = default;
FileLock::~FileLock() = default;

} // namespace ANCHORDB_NAMESPACE