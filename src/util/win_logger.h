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
#include <cstdio>
#include <ctime>
#include <sstream>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace anchordb {

class WinLogger final : public Logger {
  public:
    // Creates a logger that writes to the given file.
    //
    // The WinLogger instance takes ownership of the file handle.
    explicit WinLogger(std::FILE *fp) : fp_(fp) { assert(fp != nullptr); }

    ~WinLogger() override { std::fclose(fp_); }

    void Logv(const char *format, std::va_list arguments) override {
        // Record the time as close to the Logv() call as possible.
        FILETIME ft;
        ::GetSystemTimePreciseAsFileTime(&ft);

        // Convert to Unix time.
        ULARGE_INTEGER ticks;
        ticks.LowPart = ft.dwLowDateTime;
        ticks.HighPart = ft.dwHighDateTime;
        constexpr uint64_t k_epoch_offset = 116444736000000000ULL;
        uint64_t unix_micros = (ticks.QuadPart - k_epoch_offset) / 10;

        const std::time_t now_seconds =
            static_cast<std::time_t>(unix_micros / 1000000);
        int usec = static_cast<int>(unix_micros % 1000000);
        struct std::tm now_components;
        ::localtime_s(&now_components, &now_seconds);

        // Record the thread ID.
        constexpr const int k_max_thread_id_size = 32;
        std::ostringstream thread_stream;
        thread_stream << std::this_thread::get_id();
        std::string thread_id = thread_stream.str();
        if (thread_id.size() > k_max_thread_id_size) {
            thread_id.resize(k_max_thread_id_size);
        }

        // We first attempt to print into a stack-allocated buffer. If this
        // attempt fails, we make a second attempt with a dynamically allocated
        // buffer.
        constexpr const int k_stack_buffer_size = 512;
        char stack_buffer[k_stack_buffer_size];
        static_assert(sizeof(stack_buffer) ==
                          static_cast<size_t>(k_stack_buffer_size),
                      "sizeof(char) is expected to be 1 in C++");

        int dynamic_buffer_size = 0; // Computed in the first iteration.
        for (int iteration = 0; iteration < 2; ++iteration) {
            const int buffer_size =
                (iteration == 0) ? k_stack_buffer_size : dynamic_buffer_size;
            char *const buffer =
                (iteration == 0) ? stack_buffer : new char[dynamic_buffer_size];

            // Print the header into the buffer.
            int buffer_offset = std::snprintf(
                buffer, buffer_size, "%04d/%02d/%02d-%02d:%02d:%02d.%06d %s ",
                now_components.tm_year + 1900, now_components.tm_mon + 1,
                now_components.tm_mday, now_components.tm_hour,
                now_components.tm_min, now_components.tm_sec, usec,
                thread_id.c_str());

            // The header can be at most 28 characters (10 date + 15 time +
            // 3 delimiters) plus the thread ID, which should fit comfortably
            // into the static buffer.
            assert(buffer_offset <= 28 + k_max_thread_id_size);
            static_assert(
                28 + k_max_thread_id_size < k_stack_buffer_size,
                "stack-allocated buffer may not fit the message header");
            assert(buffer_offset < buffer_size);

            // Print the message into the buffer.
            std::va_list arguments_copy;
            va_copy(arguments_copy, arguments);
            buffer_offset += std::vsnprintf(buffer + buffer_offset,
                                            buffer_size - buffer_offset, format,
                                            arguments_copy);
            va_end(arguments_copy);

            // The code below may append a newline at the end of the buffer,
            // which requires an extra character.
            if (buffer_offset >= buffer_size - 1) {
                // The message did not fit into the buffer.
                if (iteration == 0) {
                    // Re-run the loop and use a dynamically-allocated buffer.
                    // The buffer will be large enough for the log message, an
                    // extra newline and a null terminator.
                    dynamic_buffer_size = buffer_offset + 2;
                    continue;
                }

                // The dynamically-allocated buffer was incorrectly sized. This
                // should not happen, assuming a correct implementation of
                // std::(v)snprintf. Fail in tests, recover by truncating the
                // log message in production.
                assert(false);
                buffer_offset = buffer_size - 1;
            }

            // Add a newline if necessary.
            if (buffer[buffer_offset - 1] != '\n') {
                buffer[buffer_offset] = '\n';
                ++buffer_offset;
            }

            assert(buffer_offset <= buffer_size);
            std::fwrite(buffer, 1, buffer_offset, fp_);
            std::fflush(fp_);

            if (iteration != 0) {
                delete[] buffer;
            }
            break;
        }
    }

  private:
    std::FILE *const fp_;
};

} // namespace anchordb
