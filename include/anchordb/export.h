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

#if defined(ANCHORDB_STATIC_DEFINE)
  #define ANCHORDB_EXPORT
#else
  #if defined(_WIN32) || defined(__CYGWIN__)
    #if defined(anchordb_EXPORTS)
      // Defined automatically by CMake when building the shared library
      #define ANCHORDB_EXPORT __declspec(dllexport)
    #else
      #define ANCHORDB_EXPORT __declspec(dllimport)
    #endif
  #elif defined(__GNUC__) && ((__GNUC__ >= 4) || (__GNUC__ == 3 && __GNUC_MINOR__ >= 3))
    #define ANCHORDB_EXPORT __attribute__((visibility("default")))
  #else
    #define ANCHORDB_EXPORT
  #endif
#endif
