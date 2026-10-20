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

#include <anchordb/export.h>
#include <string>
#include <string_view>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {
// Lightweight status/error result returned by most operations.
//
// A Status is cheap to copy (the common success case shares a null state) and
// is safe to hold across threads once constructed.  Use is_ok() to test for
// success and the is_*() predicates to classify an error; to_str() returns a
// printable description.
class ANCHORDB_EXPORT Status {
  public:
    // Creates a success status.
    Status() noexcept : state_(nullptr) {}
    ~Status() { delete[] state_; }

    Status(const Status &rhs) {
        state_ = (rhs.state_ == nullptr) ? nullptr : copy_state(rhs.state_);
    }
    Status &operator=(const Status &rhs) {
        // The following condition catches both aliasing (when this == &rhs),
        // and the common case where both rhs and *this are ok.
        if (state_ != rhs.state_) {
            delete[] state_;
            state_ = (rhs.state_ == nullptr) ? nullptr : copy_state(rhs.state_);
        }
        return *this;
    }

    Status(Status &&rhs) noexcept : state_(rhs.state_) { rhs.state_ = nullptr; }
    Status &operator=(Status &&rhs) noexcept {
        std::swap(state_, rhs.state_);
        return *this;
    }

    // Returns a success status.
    static Status ok() { return Status(); }

    // Factory functions returning an error status of a specific kind.  `msg`
    // is the primary description; the optional `msg2` adds secondary context
    // (e.g. a key or file name) and is omitted from the printed form when
    // empty.
    static Status not_found(std::string_view msg,
                            std::string_view msg2 = std::string_view()) {
        return Status(Code::NotFound, msg, msg2);
    }
    static Status corruption(std::string_view msg,
                             std::string_view msg2 = std::string_view()) {
        return Status(Code::Corruption, msg, msg2);
    }
    static Status not_supported(std::string_view msg,
                                std::string_view msg2 = std::string_view()) {
        return Status(Code::NotSupported, msg, msg2);
    }
    static Status invalid_argument(std::string_view msg,
                                   std::string_view msg2 = std::string_view()) {
        return Status(Code::InvalidArgument, msg, msg2);
    }
    static Status io_error(std::string_view msg,
                           std::string_view msg2 = std::string_view()) {
        return Status(Code::IoError, msg, msg2);
    }

    // Convenience overloads taking std::string, for callers that already hold
    // owned strings and do not want a temporary std::string_view.
    static Status not_found(const std::string &msg,
                            const std::string &msg2 = "") {
        return Status(Code::NotFound, msg, msg2);
    }

    static Status io_error(const std::string &msg,
                           const std::string &msg2 = "") {
        return Status(Code::IoError, msg, msg2);
    }

    // Returns true iff the status indicates success.
    bool is_ok() const { return (state_ == nullptr); }

    // Returns true iff the status indicates a NotFound error.
    bool is_not_found() const { return code() == Code::NotFound; }

    // Returns true iff the status indicates a Corruption error.
    bool is_corruption() const { return code() == Code::Corruption; }

    // Returns true iff the status indicates an IOError.
    bool is_io_error() const { return code() == Code::IoError; }

    // Returns true iff the status indicates a NotSupported error.
    bool is_not_supported() const { return code() == Code::NotSupported; }

    // Returns true iff the status indicates an InvalidArgument error.
    bool is_invalid_argument() const { return code() == Code::InvalidArgument; }

    // Returns a string representation of this status suitable for printing.
    // Returns the string "OK" for success.
    std::string to_str() const;

  private:
    enum class Code {
        Ok = 0,
        NotFound = 1,
        Corruption = 2,
        NotSupported = 3,
        InvalidArgument = 4,
        IoError = 5
    };

    Code code() const {
        return (state_ == nullptr) ? Code::Ok : static_cast<Code>(state_[4]);
    }

    Status(Code code, std::string_view msg, std::string_view msg2);
    static const char *copy_state(const char *s);

    // OK status has a null state_.  Otherwise, state_ is a new[] array
    // of the following form:
    //    state_[0..3] == length of message
    //    state_[4]    == code
    //    state_[5..]  == message
    const char *state_;
};

} // namespace ANCHORDB_NAMESPACE
