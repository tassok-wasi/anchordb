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

#include "anchordb/env.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <sstream>
#include <string>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

// A Logger that appends timestamped, thread-tagged entries to a Windows file
// stream. Every entry is emitted as a single line and flushed immediately so
// that log output survives a crash.
class WinLogger final : public Logger {
  public:
    // Creates a logger that writes to the given file.
    //
    // The WinLogger instance takes ownership of the file handle.
    explicit WinLogger(std::FILE *fp) : fp_(fp) { assert(fp_ != nullptr); }

    ~WinLogger() override { std::fclose(fp_); }

    void Logv(const char *format, std::va_list arguments) override {
        std::string entry = timestamp();
        entry += ' ';
        entry += thread_tag();
        entry += ' ';
        append_message(entry, format, arguments);
        if (entry.empty() || entry.back() != '\n') {
            entry += '\n';
        }

        std::fwrite(entry.data(), 1, entry.size(), fp_);
        std::fflush(fp_);
    }

  private:
    // Returns the current local time as "YYYY/MM/DD-HH:MM:SS.uuuuuu".
    static std::string timestamp() {
        FILETIME file_time;
        ::GetSystemTimePreciseAsFileTime(&file_time);

        // FILETIME counts 100-nanosecond intervals since 1601-01-01 UTC.
        ULARGE_INTEGER ticks;
        ticks.LowPart = file_time.dwLowDateTime;
        ticks.HighPart = file_time.dwHighDateTime;
        constexpr uint64_t k_epoch_offset = 116444736000000000ULL;
        const uint64_t unix_micros = (ticks.QuadPart - k_epoch_offset) / 10;

        const std::time_t seconds =
            static_cast<std::time_t>(unix_micros / 1000000);
        const int micros = static_cast<int>(unix_micros % 1000000);

        std::tm components{};
        ::localtime_s(&components, &seconds);

        char buffer[32];
        std::snprintf(buffer, sizeof(buffer),
                      "%04d/%02d/%02d-%02d:%02d:%02d.%06d",
                      components.tm_year + 1900, components.tm_mon + 1,
                      components.tm_mday, components.tm_hour, components.tm_min,
                      components.tm_sec, micros);
        return buffer;
    }

    // Returns the current thread's identifier, truncated to a fixed width.
    static std::string thread_tag() {
        constexpr std::size_t k_max_thread_id_size = 32;
        std::ostringstream stream;
        stream << std::this_thread::get_id();
        std::string id = stream.str();
        if (id.size() > k_max_thread_id_size) {
            id.resize(k_max_thread_id_size);
        }
        return id;
    }

    // Appends the printf-style message to entry, growing it as needed.
    static void append_message(std::string &entry, const char *format,
                               std::va_list arguments) {
        std::va_list arguments_copy;
        va_copy(arguments_copy, arguments);
        const int needed = std::vsnprintf(nullptr, 0, format, arguments_copy);
        va_end(arguments_copy);
        if (needed < 0) {
            return;
        }

        const std::size_t offset = entry.size();
        entry.resize(offset + static_cast<std::size_t>(needed) + 1);
        std::vsnprintf(entry.data() + offset, entry.size() - offset, format,
                       arguments);
        entry.resize(offset + static_cast<std::size_t>(needed));
    }

    std::FILE *const fp_;
};

} // namespace anchordb
