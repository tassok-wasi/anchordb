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

// Defines ANCHORDB_NAMESPACE, the macro that wraps every public AnchorDB
// declaration.  It defaults to `anchordb` but may be overridden at build time
// (define ANCHORDB_NAMESPACE before including any AnchorDB header) so the whole
// library can be relocated into another namespace.

#pragma once

#ifndef ANCHORDB_NAMESPACE
    #define ANCHORDB_NAMESPACE anchordb
#endif
