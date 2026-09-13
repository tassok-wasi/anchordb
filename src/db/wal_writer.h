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

#include "anchordb/slice.h"
#include "anchordb/status.h"
namespace anchordb {

class WritableFileWriter;

class WALWriter {
  public:
    explicit WALWriter(WritableFileWriter *dest);

    WALWriter(const WALWriter &) = delete;
    WALWriter &operator=(const WALWriter &) = delete;

    ~WALWriter();

    Status append(const Slice &data);
    Status flush();

  private:
    WritableFileWriter *dest_;
};

} // namespace anchordb
