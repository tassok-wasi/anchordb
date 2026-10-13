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

#include "anchordb/env.h"
#include "anchordb/status.h"
#include "db/wal_writer.h"
#include "util/coding.h"
#include "util/crc32.h"
#include <cstdint>
namespace anchordb {

WALWriter::WALWriter(WritableFileWriter *dest) : dest_(dest) {}

WALWriter::~WALWriter() = default;

Status WALWriter::append(std::string_view sv) {
    // 4 bytes crc, at most 5 bytes for varint32 length
    char buf[9];

    uint32_t crc = value(sv.data(), sv.size());
    crc = mask(crc);
    encode_fixed_32(buf, crc);
    char *ptr = encode_varint_32(buf + 4, sv.size());

    Status s = dest_->append(std::string_view(buf, (ptr - buf)));
    if (s.is_ok()) {
        s = dest_->append(sv);
    }
    return s;
}

Status WALWriter::flush() { return dest_->flush(); }

} // namespace anchordb
