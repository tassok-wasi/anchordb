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
#include "anchordb/compressor.h"
#include "anchordb/export.h"
#include "anchordb/filter.h"
#include <cstddef>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

class Cache;
class Env;
class Logger;
class Snapshot;

enum class ANCHORDB_EXPORT MapType {
    RobbinHood = 0x0,
    LinearProbing = 0x1,
    Swiss = 0x2,
};

struct ANCHORDB_EXPORT Options {
    Options();

    bool create_if_mission = false;
    bool error_if_exists = false;
    bool paranoid_checks = false;
    Env *env;
    Logger *info_log = nullptr;
    size_t write_buffer_size = 4 * 1024 * 1024;
    int max_open_files = 1000;
    MapType active_map = MapType::Swiss;
    Cache *block_cache = nullptr;
    size_t block_size = 4 * 1024;
    int block_restart_interval = 16;
    size_t max_file_size = 2 * 1024 * 1024;
    CompressionType active_compression_type = CompressionType::Snappy;
    FilterType active_filter_type = FilterType::Bloom;
    int zstd_compression_level = 1;
    bool reuse_logs = false;
    Comparator comparator = Comparator{};
    Compressor compressor = Compressor{};
    Filter filter = Filter{};
};

struct ANCHORDB_EXPORT ReadOptions {
    bool verify_checksums = false;
    bool fill_cache = true;
};

struct ANCHORDB_EXPORT WriteOptions {
    WriteOptions() = default;

    bool sync = false;
};

} // namespace anchordb
