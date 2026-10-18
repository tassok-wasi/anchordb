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

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

Env::~Env() = default;
EnvWrapper::~EnvWrapper() = default;

SequentialFileReader::~SequentialFileReader() = default;
RandomAccessFileReader::~RandomAccessFileReader() = default;
WritableFileWriter::~WritableFileWriter() = default;

Logger::~Logger() = default;
FileLock::~FileLock() = default;

} // namespace ANCHORDB_NAMESPACE