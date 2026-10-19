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

#include "anchordb/anchordb_namespace.h"
#include <cstdint>
#include <string>
#include <string_view>

namespace ANCHORDB_NAMESPACE {

// Logical role of a file inside a database directory.  The role fixes the
// file's suffix (or its whole name for singleton files) so a name is
// self-describing and can be classified again during recovery.
enum class FileType : uint8_t {
    kLogFile = 0,      // <db>/<number>.log      write-ahead log
    kTableDataFile,    // <db>/<number>.data     SSTable data
    kTableIndexFile,   // <db>/<number>.index    SSTable index
    kTableSummaryFile, // <db>/<number>.summary  SSTable summary
    kVLogFile,         // <db>/<number>.vlog     WiscKey value log
    kDescriptorFile,   // <db>/MANIFEST-<number> version edit log
    kTempFile,         // <db>/<number>.dbtmp    temporary file
    kCurrentFile,      // <db>/CURRENT           points at the live MANIFEST
    kLockFile,         // <db>/LOCK              single-process lock
    kInfoLogFile,      // <db>/LOG               human-readable info log
};

// Builds "<dbname>/<number zero-padded to 6><suffix>".  `dbname` may be empty,
// in which case no directory prefix is written.  `suffix` is appended verbatim,
// so it should include a leading '.' for ".log"-style names.
std::string MakeFileName(std::string_view dbname, uint64_t number,
                         std::string_view suffix);

// Numbered file builders.
std::string LogFileName(std::string_view dbname, uint64_t number);
std::string TableDataFileName(std::string_view dbname, uint64_t number);
std::string TableIndexFileName(std::string_view dbname, uint64_t number);
std::string TableSummaryFileName(std::string_view dbname, uint64_t number);
std::string VLogFileName(std::string_view dbname, uint64_t number);
std::string DescriptorFileName(std::string_view dbname, uint64_t number);
std::string TempFileName(std::string_view dbname, uint64_t number);

// Singleton (fixed-name) file builders.
std::string CurrentFileName(std::string_view dbname);
std::string LockFileName(std::string_view dbname);
std::string InfoLogFileName(std::string_view dbname);

// Dispatches to the builder for `type`.  Numbered types use `number`; singleton
// types ignore it.  Returns an empty string for an unrecognized type.
std::string FileName(std::string_view dbname, uint64_t number, FileType type);

// Recovers the (number, type) encoded in a generated file name, ignoring any
// leading directory.  Returns false if the name matches no known pattern.
bool ParseFileName(std::string_view filename, uint64_t *number, FileType *type);

} // namespace ANCHORDB_NAMESPACE
