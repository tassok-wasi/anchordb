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

namespace anchordb {

class FileLock;
class Logger;
class AlignedBuffer;
class RandomAccessFileReader;
class SequentialFileReader;
class WritableFileWriter;
class Logger;

class ANCHORDB_EXPORT Env {
  public:
    Env() = default;

    Env(const Env &) = delete;
    Env &operator=(const Env &) = delete;

    virtual ~Env();

    static Env *default_env();

    virtual Status new_sequential_file_reader(const std::string &fname,
                                              SequentialFileReader **result,
                                              size_t buffer_size = 0) = 0;
    virtual Status
    new_random_access_file_reader(const std::string &fname,
                                  RandomAccessFileReader **result) = 0;
    virtual Status new_writable_file_writer(const std::string &fname,
                                            WritableFileWriter **result,
                                            size_t buffer_size = 0) = 0;
    virtual Status new_appendable_file_writer(const std::string &fname,
                                              WritableFileWriter **result,
                                              size_t buffer_size = 0) = 0;

    virtual bool file_exists(const std::string &fname) = 0;
    virtual Status get_children(const std::string &dir,
                                std::vector<std::string> *children) = 0;
    virtual Status remove_file(const std::string &fname) = 0;
    virtual Status create_dir(const std::string &dirname) = 0;
    virtual Status remove_dir(const std::string &dirname) = 0;
    virtual Status get_file_size(const std::string &fname, uint64_t *size) = 0;
    virtual Status rename_file(const std::string &src,
                               const std::string &target) = 0;

    virtual Status lock_file(const std::string &fname, FileLock **lock) = 0;
    virtual Status unlock_file(FileLock *lock) = 0;

    virtual void schedule(void (*function)(void *arg), void *arg) = 0;
    virtual Status get_test_dir(std::string *path) = 0;

    virtual Status new_logger(const std::string &fname, Logger **result) = 0;

    virtual uint64_t now_micros() = 0;
    virtual void sleep_for_micros(int micros) = 0;
};

class ANCHORDB_EXPORT SequentialFileReader {
  public:
    SequentialFileReader() = default;
    virtual ~SequentialFileReader();

    SequentialFileReader(const SequentialFileReader &) = delete;
    SequentialFileReader &operator=(const SequentialFileReader &) = delete;

    virtual Status read(size_t n, std::string_view *result, char *scratch) = 0;
    virtual Status skip(uint64_t n) = 0;
};

class ANCHORDB_EXPORT RandomAccessFileReader {
  public:
    RandomAccessFileReader() = default;

    RandomAccessFileReader(const RandomAccessFileReader &) = delete;
    RandomAccessFileReader &operator=(const RandomAccessFileReader &) = delete;

    virtual ~RandomAccessFileReader();

    virtual Status read(uint64_t offset, size_t n, std::string_view *result,
                        char *scratch) = 0;
};

class ANCHORDB_EXPORT WritableFileWriter {
  public:
    WritableFileWriter() = default;

    WritableFileWriter(const WritableFileWriter &) = delete;
    WritableFileWriter &operator=(const WritableFileWriter &) = delete;

    virtual ~WritableFileWriter();

    virtual Status append(std::string_view data) = 0;
    virtual Status close() = 0;
    virtual Status flush() = 0;
    virtual Status sync() = 0;
};

class ANCHORDB_EXPORT Logger {
  public:
    Logger() = default;

    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    virtual ~Logger();

    // Write an entry to the log file with the specified format.
    virtual void Logv(const char *format, std::va_list ap) = 0;
};

class ANCHORDB_EXPORT FileLock {
  public:
    FileLock() = default;

    FileLock(const FileLock &) = delete;
    FileLock &operator=(const FileLock &) = delete;

    virtual ~FileLock();
};

class ANCHORDB_EXPORT EnvWrapper : public Env {
  public:
    // Initialize an EnvWrapper that delegates all calls to *t.
    explicit EnvWrapper(Env *t) : target_(t) {}
    virtual ~EnvWrapper();

    // Return the target to which this Env forwards all calls.
    Env *target() const { return target_; }

    // The following text is boilerplate that forwards all methods to target().
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

} // namespace anchordb
