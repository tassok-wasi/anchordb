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
#include <chrono>
#include <cstdio>
#include <ctime>
#include <sstream>
#include <string>
#include <thread>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

// A Logger that appends timestamped, thread-tagged entries to a POSIX file
// stream. Every entry is emitted as a single line and flushed immediately so
// that log output survives a crash.
class PosixLogger final : public Logger {
  public:
    // Creates a logger that writes to the given file.
    //
    // The PosixLogger instance takes ownership of the file handle.
    explicit PosixLogger(std::FILE *fp) : fp_(fp) { assert(fp_ != nullptr); }

    ~PosixLogger() override { std::fclose(fp_); }

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
        using namespace std::chrono;
        const auto now = system_clock::now();
        const auto micros =
            duration_cast<microseconds>(now.time_since_epoch()).count() %
            1000000;
        const std::time_t seconds = system_clock::to_time_t(now);

        std::tm components{};
        ::localtime_r(&seconds, &components);

        char buffer[32];
        std::snprintf(buffer, sizeof(buffer),
                      "%04d/%02d/%02d-%02d:%02d:%02d.%06d",
                      components.tm_year + 1900, components.tm_mon + 1,
                      components.tm_mday, components.tm_hour, components.tm_min,
                      components.tm_sec, static_cast<int>(micros));
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
