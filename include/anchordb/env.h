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

#include "anchordb/export.h"
#include "anchordb/status.h"
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

class FileLock;
class Logger;
class AlignedBuffer;
class RandomAccessFileReader;
class SequentialFileReader;
class WritableFileWriter;
class Logger;

// Operating-system abstraction: every filesystem, locking, scheduling and
// timing call the engine makes goes through an Env, which makes the engine
// portable and testable (a test can install its own Env).
//
// Obtain the platform default with default_env().  If nothing has been
// installed it is created on first use and owned by the library.
class ANCHORDB_EXPORT Env {
  public:
    Env() = default;

    Env(const Env &) = delete;
    Env &operator=(const Env &) = delete;

    virtual ~Env();

    // Returns the process-wide Env, creating the platform default on first
    // call.  Never null.
    static Env *default_env();

    // Overrides the environment returned by default_env().  Pass nullptr to
    // clear the override and fall back to the platform default.  The caller
    // retains ownership and must keep `env` alive while it is installed.
    static void set_default_env(Env *env);

    // Opens `fname` for sequential reading.  `buffer_size` of 0 selects a
    // sensible default.
    virtual Status new_sequential_file_reader(const std::string &fname,
                                              SequentialFileReader **result,
                                              size_t buffer_size = 0) = 0;
    // Opens `fname` for random (positional) reading.
    virtual Status
    new_random_access_file_reader(const std::string &fname,
                                  RandomAccessFileReader **result) = 0;
    // Creates (or truncates) `fname` for buffered writing.
    virtual Status new_writable_file_writer(const std::string &fname,
                                            WritableFileWriter **result,
                                            size_t buffer_size = 0) = 0;
    // Opens `fname` for appending, preserving any existing contents.
    virtual Status new_appendable_file_writer(const std::string &fname,
                                              WritableFileWriter **result,
                                              size_t buffer_size = 0) = 0;

    // Returns whether `fname` exists.
    virtual bool file_exists(const std::string &fname) = 0;
    // Appends the names of the entries in directory `dir` to `*children`.
    virtual Status get_children(const std::string &dir,
                                std::vector<std::string> *children) = 0;
    // Deletes the file `fname`.
    virtual Status remove_file(const std::string &fname) = 0;
    // Creates directory `dirname`.
    virtual Status create_dir(const std::string &dirname) = 0;
    // Removes the empty directory `dirname`.
    virtual Status remove_dir(const std::string &dirname) = 0;
    // Stores the size in bytes of `fname` in `*size`.
    virtual Status get_file_size(const std::string &fname, uint64_t *size) = 0;
    // Renames `src` to `target`, replacing `target` if it exists.
    virtual Status rename_file(const std::string &src,
                               const std::string &target) = 0;

    // Acquires an exclusive lock on `fname`, storing the lock token in `*lock`.
    // The caller owns the token and releases it with unlock_file().
    virtual Status lock_file(const std::string &fname, FileLock **lock) = 0;
    // Releases a lock previously obtained from lock_file().
    virtual Status unlock_file(FileLock *lock) = 0;

    // Schedules `function(arg)` to run on a background thread.  Returns
    // immediately; the work is best-effort and may be dropped if the Env is
    // shutting down.
    virtual void schedule(void (*function)(void *arg), void *arg) = 0;
    // Returns a directory suitable for temporary test files.
    virtual Status get_test_dir(std::string *path) = 0;

    // Opens `fname` as an append-only logger, storing it in `*result`.
    virtual Status new_logger(const std::string &fname, Logger **result) = 0;

    // Returns the current time in microseconds since the Unix epoch.
    virtual uint64_t now_micros() = 0;
    // Sleeps for approximately `micros` microseconds.
    virtual void sleep_for_micros(int micros) = 0;
};

// Reads a file as a forward-only byte stream.
class ANCHORDB_EXPORT SequentialFileReader {
  public:
    SequentialFileReader() = default;
    virtual ~SequentialFileReader();

    SequentialFileReader(const SequentialFileReader &) = delete;
    SequentialFileReader &operator=(const SequentialFileReader &) = delete;

    // Reads up to `n` bytes into `scratch` and points `*result` at them; the
    // view is valid until the next call.  `*result` may be shorter than `n` at
    // end of file.
    virtual Status read(size_t n, std::string_view *result, char *scratch) = 0;
    // Advances the read position by `n` bytes without copying.
    virtual Status skip(uint64_t n) = 0;
};

// Reads a file at arbitrary offsets.
class ANCHORDB_EXPORT RandomAccessFileReader {
  public:
    RandomAccessFileReader() = default;

    RandomAccessFileReader(const RandomAccessFileReader &) = delete;
    RandomAccessFileReader &operator=(const RandomAccessFileReader &) = delete;

    virtual ~RandomAccessFileReader();

    // Reads up to `n` bytes at absolute `offset` into `scratch` and points
    // `*result` at them; the view is valid until the next call.  `*result` may
    // be shorter than `n` at end of file.
    virtual Status read(uint64_t offset, size_t n, std::string_view *result,
                        char *scratch) = 0;
};

// Writes a file as an append-only byte stream.
class ANCHORDB_EXPORT WritableFileWriter {
  public:
    WritableFileWriter() = default;

    WritableFileWriter(const WritableFileWriter &) = delete;
    WritableFileWriter &operator=(const WritableFileWriter &) = delete;

    virtual ~WritableFileWriter();

    // Appends `data`; the bytes may be buffered until flush()/sync()/close().
    virtual Status append(std::string_view data) = 0;
    // Flushes buffered data and closes the file.
    virtual Status close() = 0;
    // Flushes buffered data to the OS (not necessarily durable).
    virtual Status flush() = 0;
    // Flushes and fsyncs, making prior appends durable.
    virtual Status sync() = 0;
};

// Append-only text logger.
class ANCHORDB_EXPORT Logger {
  public:
    Logger() = default;

    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    virtual ~Logger();

    // Writes one formatted entry (printf-style, with `ap`) to the log file.
    virtual void Logv(const char *format, std::va_list ap) = 0;
};

// Token for an acquired file lock; the type is opaque to callers and is only
// used to pass the lock back to Env::unlock_file().
class ANCHORDB_EXPORT FileLock {
  public:
    FileLock() = default;

    FileLock(const FileLock &) = delete;
    FileLock &operator=(const FileLock &) = delete;

    virtual ~FileLock();
};

// Env decorator that forwards every call to a target Env.  Subclass it and
// override only the methods you need, e.g. to intercept file creation in a
// test; all other calls keep their default behavior.
class ANCHORDB_EXPORT EnvWrapper : public Env {
  public:
    // Wraps `t`; all calls are delegated to it.  `t` must outlive the wrapper.
    explicit EnvWrapper(Env *t) : target_(t) {}
    virtual ~EnvWrapper();

    // Returns the wrapped Env.
    Env *target() const { return target_; }

    // The rest of the class forwards every Env method to target().
    Status new_sequential_file_reader(const std::string &f,
                                      SequentialFileReader **r,
                                      size_t buffer_size = 0) override {
        return target_->new_sequential_file_reader(f, r, buffer_size);
    }
    Status new_random_access_file_reader(const std::string &f,
                                         RandomAccessFileReader **r) override {
        return target_->new_random_access_file_reader(f, r);
    }
    Status new_writable_file_writer(const std::string &f,
                                    WritableFileWriter **r,
                                    size_t buffer_size = 0) override {
        return target_->new_writable_file_writer(f, r, buffer_size);
    }
    Status new_appendable_file_writer(const std::string &f,
                                      WritableFileWriter **r,
                                      size_t buffer_size = 0) override {
        return target_->new_appendable_file_writer(f, r, buffer_size);
    }
    bool file_exists(const std::string &f) override {
        return target_->file_exists(f);
    }
    Status get_children(const std::string &dir,
                        std::vector<std::string> *r) override {
        return target_->get_children(dir, r);
    }
    Status remove_file(const std::string &f) override {
        return target_->remove_file(f);
    }
    Status create_dir(const std::string &d) override {
        return target_->create_dir(d);
    }
    Status remove_dir(const std::string &d) override {
        return target_->remove_dir(d);
    }
    Status get_file_size(const std::string &f, uint64_t *s) override {
        return target_->get_file_size(f, s);
    }
    Status rename_file(const std::string &s, const std::string &t) override {
        return target_->rename_file(s, t);
    }
    Status lock_file(const std::string &f, FileLock **l) override {
        return target_->lock_file(f, l);
    }
    Status unlock_file(FileLock *l) override { return target_->unlock_file(l); }
    void schedule(void (*f)(void *), void *a) override {
        return target_->schedule(f, a);
    }
    Status get_test_dir(std::string *path) override {
        return target_->get_test_dir(path);
    }
    Status new_logger(const std::string &fname, Logger **result) override {
        return target_->new_logger(fname, result);
    }
    uint64_t now_micros() override { return target_->now_micros(); }
    void sleep_for_micros(int micros) override {
        target_->sleep_for_micros(micros);
    }

  private:
    Env *target_;
};

} // namespace ANCHORDB_NAMESPACE
