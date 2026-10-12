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

// Native thread primitives used by ThreadPool.
//
// std::thread reports creation failure by throwing std::system_error and has
// no non-throwing variant. In an exception-free build that means a failed
// creation calls std::terminate, and a partially constructed pool cannot clean
// up the threads it already started. These wrappers report failure through a
// return value instead, so the pool can degrade gracefully.

#if defined(_WIN32)
    #include <process.h>
    #include <windows.h>
#else
    #include <pthread.h>
#endif

#if defined(_WIN32)

using NativeThreadHandle = HANDLE;
using NativeThreadEntry = unsigned(__stdcall *)(void *);

inline bool thread_create(NativeThreadHandle &handle, NativeThreadEntry entry,
                          void *arg) {
    handle = reinterpret_cast<HANDLE>(
        ::_beginthreadex(nullptr, 0, entry, arg, 0, nullptr));
    return handle != nullptr;
}

inline void thread_join(NativeThreadHandle handle) {
    if (handle != nullptr) {
        ::WaitForSingleObject(handle, INFINITE);
        ::CloseHandle(handle);
    }
}

#else

using NativeThreadHandle = pthread_t;
using NativeThreadEntry = void *(*)(void *);

inline bool thread_create(NativeThreadHandle &handle, NativeThreadEntry entry,
                          void *arg) {
    return ::pthread_create(&handle, nullptr, entry, arg) == 0;
}

inline void thread_join(NativeThreadHandle handle) {
    ::pthread_join(handle, nullptr);
}

#endif
