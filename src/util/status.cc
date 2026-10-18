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

#include "anchordb/status.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

const char *Status::copy_state(const char *state) {
    uint32_t size;
    std::memcpy(&size, state, sizeof(size));
    char *result = new char[size + 5];
    std::memcpy(result, state, size + 5);
    return result;
}

Status::Status(Code code, std::string_view msg, std::string_view msg2) {
    assert(code != Code::Ok);
    const uint32_t len1 = static_cast<uint32_t>(msg.size());
    const uint32_t len2 = static_cast<uint32_t>(msg2.size());
    const uint32_t size = len1 + (len2 ? (2 + len2) : 0);
    char *result = new char[size + 5];
    std::memcpy(result, &size, sizeof(size));
    result[4] = static_cast<char>(code);
    std::memcpy(result + 5, msg.data(), len1);
    if (len2) {
        result[5 + len1] = ':';
        result[6 + len1] = ' ';
        std::memcpy(result + 7 + len1, msg2.data(), len2);
    }
    state_ = result;
}

std::string Status::to_str() const {
    if (state_ == nullptr) {
        return "OK";
    } else {
        char tmp[30];
        const char *type;
        switch (code()) {
        case Code::Ok: type = "OK"; break;
        case Code::NotFound: type = "NotFound: "; break;
        case Code::Corruption: type = "Corruption: "; break;
        case Code::NotSupported: type = "Not implemented: "; break;
        case Code::InvalidArgument: type = "Invalid argument: "; break;
        case Code::IoError: type = "IO error: "; break;
        default:
            std::snprintf(tmp, sizeof(tmp),
                          "Unknown code(%d): ", static_cast<int>(code()));
            type = tmp;
            break;
        }
        std::string result(type);
        uint32_t length;
        std::memcpy(&length, state_, sizeof(length));
        result.append(state_ + 5, length);
        return result;
    }
}

} // namespace ANCHORDB_NAMESPACE
