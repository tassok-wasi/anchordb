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

// Centralizes the io_uring availability switch. CMake defines HAS_IO_URING to
// 0 or 1; default to 0 so translation units built outside the build system
// still compile without liburing. When disabled, `struct io_uring` remains an
// incomplete type and only pointers to it may be used.

#ifndef HAS_IO_URING
    #define HAS_IO_URING 0
#endif

#if HAS_IO_URING
    #include <liburing.h>
#else
struct io_uring;
#endif
