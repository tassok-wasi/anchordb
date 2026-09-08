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
#include "anchordb/slice.h"
#include "anchordb/status.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace anchordb {

class FileLock;
class Logger;
class AlignedBuffer;
class RandomAccessFile;
class SequentialReadFile;
class WritableFile;

class ANCHORDB_EXPORT Env {
  public:
    Env() = default;

    Env(const Env &) = delete;
    Env &operator=(const Env &) = delete;

    virtual ~Env();

    static Env *default_env();

    virtual AlignedBuffer *new_aligned_buffer(size_t capacity,
                                              size_t alignment = 4096) = 0;
    virtual Status new_sequential_read_file(const std::string &fname,
                                            SequentialReadFile **result) = 0;
    virtual Status new_random_access_file(const std::string &fname,
                                          RandomAccessFile **result) = 0;
    virtual Status new_writable_file(const std::string &fname,
                                     AlignedBuffer **buf,
                                     WritableFile **result) = 0;
    virtual Status new_appendable_file(const std::string &fname,
                                       AlignedBuffer **buf,
                                       WritableFile **result) = 0;

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

    virtual uint64_t now_micros() = 0;
    virtual void sleep_for_micros(int micros) = 0;
};

class ANCHORDB_EXPORT AlignedBuffer {
  public:
    AlignedBuffer() = default;
    virtual ~AlignedBuffer() = default;

    AlignedBuffer(const AlignedBuffer &) = delete;
    AlignedBuffer &operator=(const AlignedBuffer &) = delete;

    virtual void allocate(size_t capacity, size_t alignment) = 0;
    virtual void free() = 0;

    virtual char *data() const = 0;
    virtual size_t capacity() const = 0;
    virtual size_t alignment() const = 0;
    virtual bool is_allocated() const = 0;
};

class ANCHORDB_EXPORT SequentialReadFile {
  public:
    SequentialReadFile() = default;
    virtual ~SequentialReadFile();

    SequentialReadFile(const SequentialReadFile &) = delete;
    SequentialReadFile &operator=(const SequentialReadFile &) = delete;

    virtual Status read(size_t n, Slice *result, char *scratch) = 0;
    virtual Status skip(uint64_t n) = 0;
};

class ANCHORDB_EXPORT RandomAccessFile {
  public:
    RandomAccessFile() = default;

    RandomAccessFile(const RandomAccessFile &) = delete;
    RandomAccessFile &operator=(const RandomAccessFile &) = delete;

    virtual ~RandomAccessFile();

    virtual Status read(uint64_t offset, size_t n, Slice *result,
                        char *scratch) = 0;
};

class ANCHORDB_EXPORT WritableFile {
  public:
    WritableFile() = default;

    WritableFile(const WritableFile &) = delete;
    WritableFile &operator=(const WritableFile &) = delete;

    virtual ~WritableFile();

    virtual Status append(const Slice &data) = 0;
    virtual Status close() = 0;
    virtual Status flush() = 0;
    virtual Status sync() = 0;
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
    Status new_sequential_read_file(const std::string &f,
                                    SequentialReadFile **r) override {
        return target_->new_sequential_read_file(f, r);
    }
    Status new_random_access_file(const std::string &f,
                                  RandomAccessFile **r) override {
        return target_->new_random_access_file(f, r);
    }
    Status new_writable_file(const std::string &f, AlignedBuffer **buf,
                             WritableFile **r) override {
        return target_->new_writable_file(f, buf, r);
    }
    Status new_appendable_file(const std::string &f, AlignedBuffer **buf,
                               WritableFile **r) override {
        return target_->new_appendable_file(f, buf, r);
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
    uint64_t now_micros() override { return target_->now_micros(); }
    void sleep_for_micros(int micros) override {
        target_->sleep_for_micros(micros);
    }

  private:
    Env *target_;
};

} // namespace anchordb
