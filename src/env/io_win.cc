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

#include "anchordb/status.h"
#include "io_win.h"
#include "util/aligned_buffer.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace anchordb {

Status win_error(const std::string &context, DWORD error_number) {
    if (error_number == ERROR_FILE_NOT_FOUND ||
        error_number == ERROR_PATH_NOT_FOUND) {
        char buf[256];
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM |
                           FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, error_number, 0, buf, sizeof(buf), nullptr);
        return Status::not_found(context, buf);
    } else {
        char buf[256];
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM |
                           FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, error_number, 0, buf, sizeof(buf), nullptr);
        return Status::io_error(context, buf);
    }
}

// ---------------------------------------------------------------------------
// WinSequentialFileReader
// ---------------------------------------------------------------------------

Status WinSequentialFileReader::submit_read_(void *dst, size_t len,
                                             uint64_t offset,
                                             size_t *bytes_read) {
    *bytes_read = 0;
    size_t total_read = 0;

    while (total_read < len) {
        OVERLAPPED overlapped = {};
        overlapped.Offset = static_cast<DWORD>(offset + total_read);
        overlapped.OffsetHigh = static_cast<DWORD>((offset + total_read) >> 32);

        void *current_dst = static_cast<char *>(dst) + total_read;
        size_t current_len = len - total_read;

        // Clamp to DWORD_MAX for a single ReadFile call.
        DWORD chunk = (current_len > MAXDWORD)
                          ? MAXDWORD
                          : static_cast<DWORD>(current_len);

        DWORD bytes_transferred = 0;
        BOOL ok = ::ReadFile(hFile_, current_dst, chunk, &bytes_transferred,
                             &overlapped);

        if (!ok) {
            DWORD err = ::GetLastError();
            if (err != ERROR_IO_PENDING) {
                return win_error(filename_, err);
            }
            // Wait for the overlapped operation to complete.
            if (!::GetOverlappedResult(hFile_, &overlapped, &bytes_transferred,
                                       TRUE)) {
                return win_error(filename_, ::GetLastError());
            }
        }

        if (bytes_transferred == 0) {
            break; // EOF
        }

        total_read += static_cast<size_t>(bytes_transferred);

        if (direct_io_ && (bytes_transferred % k_o_direct_alignment != 0)) {
            break; // Short read under O_DIRECT.
        }
    }

    *bytes_read = total_read;
    return Status::ok();
}

Status WinSequentialFileReader::read(size_t n, Slice *result, char *scratch) {
    *result = Slice();
    if (n == 0)
        return Status::ok();

    size_t total_copied = 0;

    while (total_copied < n) {
        // Serve from the internal buffer if the current offset falls within it.
        if (offset_ >= buf_file_offset_ &&
            offset_ < buf_file_offset_ + buf_bytes_valid_) {

            size_t buf_internal_offset =
                static_cast<size_t>(offset_ - buf_file_offset_);
            size_t available_bytes = buf_bytes_valid_ - buf_internal_offset;
            size_t bytes_to_copy = std::min(n - total_copied, available_bytes);

            std::memcpy(scratch + total_copied,
                        buf_.data() + buf_internal_offset, bytes_to_copy);

            total_copied += bytes_to_copy;
            offset_ += bytes_to_copy;

            if (total_copied == n)
                break;
        }

        if (!direct_io_) {
            size_t bytes_read = 0;
            Status s = submit_read_(scratch + total_copied, n - total_copied,
                                    offset_, &bytes_read);
            if (!s.is_ok())
                return s;

            if (bytes_read == 0)
                break;
            total_copied += bytes_read;
            offset_ += bytes_read;
            break;
        }

        // O_DIRECT path: reads must be sector-aligned.
        size_t remaining_req = n - total_copied;
        bool is_dst_aligned =
            (reinterpret_cast<uintptr_t>(scratch + total_copied) %
                 k_o_direct_alignment ==
             0);
        bool is_offset_aligned = (offset_ % k_o_direct_alignment == 0);

        // Fast path: scratch is aligned and we have enough to fill a full
        // aligned read directly into scratch.
        if (remaining_req >= buf_.capacity() && is_dst_aligned &&
            is_offset_aligned) {
            size_t aligned_len =
                remaining_req & ~static_cast<size_t>(k_o_direct_alignment - 1);
            size_t bytes_read = 0;

            Status s = submit_read_(scratch + total_copied, aligned_len,
                                    offset_, &bytes_read);
            if (!s.is_ok())
                return s;

            total_copied += bytes_read;
            offset_ += bytes_read;

            buf_bytes_valid_ = 0;

            if (bytes_read < aligned_len)
                break;
            continue;
        }

        // Slow path: read into the internal aligned buffer, then copy out.
        uint64_t aligned_offset =
            offset_ & ~static_cast<uint64_t>(k_o_direct_alignment - 1);
        size_t bytes_read = 0;

        Status s = submit_read_(buf_.data(), buf_.capacity(), aligned_offset,
                                &bytes_read);
        if (!s.is_ok())
            return s;

        buf_file_offset_ = aligned_offset;
        buf_bytes_valid_ = bytes_read;

        if (offset_ >= buf_file_offset_ + buf_bytes_valid_) {
            break;
        }
    }

    *result = Slice(scratch, total_copied);
    return Status::ok();
}

Status WinSequentialFileReader::skip(uint64_t n) {
    offset_ += n;

    if (offset_ < buf_file_offset_ ||
        offset_ >= buf_file_offset_ + buf_bytes_valid_) {
        buf_bytes_valid_ = 0;
    }

    // For non-direct I/O, reposition the file pointer.
    if (!direct_io_) {
        LARGE_INTEGER distance;
        distance.QuadPart = static_cast<LONGLONG>(offset_);
        if (!::SetFilePointerEx(hFile_, distance, nullptr, FILE_BEGIN)) {
            return win_error(filename_, ::GetLastError());
        }
    }

    return Status::ok();
}

// ---------------------------------------------------------------------------
// WinRandomAccessFileReader
// ---------------------------------------------------------------------------

Status WinRandomAccessFileReader::submit_read_(HANDLE hFile, void *buf,
                                               size_t n, uint64_t offset,
                                               size_t *bytes_read) {
    *bytes_read = 0;
    size_t total_read = 0;

    while (total_read < n) {
        OVERLAPPED overlapped = {};
        overlapped.Offset = static_cast<DWORD>(offset + total_read);
        overlapped.OffsetHigh = static_cast<DWORD>((offset + total_read) >> 32);

        void *current_dst = static_cast<char *>(buf) + total_read;
        size_t current_len = n - total_read;

        DWORD chunk = (current_len > MAXDWORD)
                          ? MAXDWORD
                          : static_cast<DWORD>(current_len);

        DWORD bytes_transferred = 0;
        BOOL ok = ::ReadFile(hFile, current_dst, chunk, &bytes_transferred,
                             &overlapped);

        if (!ok) {
            DWORD err = ::GetLastError();
            if (err != ERROR_IO_PENDING) {
                return win_error(filename_, err);
            }
            if (!::GetOverlappedResult(hFile, &overlapped, &bytes_transferred,
                                       TRUE)) {
                return win_error(filename_, ::GetLastError());
            }
        }

        if (bytes_transferred == 0) {
            // EOF reached.
            *bytes_read = total_read;
            return Status::ok();
        }

        total_read += static_cast<size_t>(bytes_transferred);

        if (direct_io_ && (bytes_transferred % k_o_direct_alignment != 0)) {
            // Short read under O_DIRECT; stop here.
            *bytes_read = total_read;
            return Status::ok();
        }
    }

    *bytes_read = total_read;
    return Status::ok();
}

Status WinRandomAccessFileReader::read(uint64_t offset, size_t n, Slice *result,
                                       char *scratch) {
    *result = Slice();
    HANDLE target_handle = INVALID_HANDLE_VALUE;

    if (has_permanent_handle_) {
        target_handle = hFile_;
    } else {
        // Open a temporary handle for this read.
        DWORD flags_and_attributes = FILE_ATTRIBUTE_NORMAL;
        if (direct_io_) {
            flags_and_attributes |= FILE_FLAG_NO_BUFFERING;
        }
        flags_and_attributes |= FILE_FLAG_OVERLAPPED;

        target_handle = ::CreateFileA(filename_.c_str(), GENERIC_READ,
                                      FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                      flags_and_attributes, nullptr);
        if (target_handle == INVALID_HANDLE_VALUE) {
            return win_error(filename_, ::GetLastError());
        }
    }

    struct TempHandleGuard {
        HANDLE handle;
        bool is_temp;
        ~TempHandleGuard() {
            if (is_temp && handle != INVALID_HANDLE_VALUE) {
                ::CloseHandle(handle);
            }
        }
    } guard{target_handle, !has_permanent_handle_};

    uint64_t read_offset = offset;
    size_t read_bytes = n;
    char *target_buf = scratch;

    // AlignedBuffer that outlives the submit_read_ call when direct I/O
    // requires a bounce buffer.
    AlignedBuffer aligned_buf;

    if (direct_io_) {
        bool is_scratch_aligned =
            (reinterpret_cast<uintptr_t>(scratch) % k_o_direct_alignment == 0);
        bool is_offset_aligned = (offset % k_o_direct_alignment == 0);
        bool is_len_aligned = (n % k_o_direct_alignment == 0);

        if (is_scratch_aligned && is_offset_aligned && is_len_aligned) {
            target_buf = scratch;
        } else {
            read_offset =
                offset & ~(static_cast<uint64_t>(k_o_direct_alignment) - 1);
            uint64_t end_offset =
                ((offset + n) + k_o_direct_alignment - 1) &
                ~(static_cast<uint64_t>(k_o_direct_alignment) - 1);
            read_bytes = static_cast<size_t>(end_offset - read_offset);

            aligned_buf = AlignedBuffer(read_bytes, k_o_direct_alignment);
            if (!aligned_buf.is_allocated()) {
                return Status::io_error(
                    "Failed to allocate sector-aligned buffer for O_DIRECT");
            }
            target_buf = aligned_buf.data();
        }
    }

    size_t bytes_read = 0;
    Status status = submit_read_(target_handle, target_buf, read_bytes,
                                 read_offset, &bytes_read);
    if (!status.is_ok()) {
        return status;
    }

    if (direct_io_) {
        uint64_t internal_offset = offset - read_offset;

        if (bytes_read <= internal_offset) {
            *result = Slice(scratch, 0);
            return Status::ok();
        }

        size_t actual_payload_bytes =
            std::min(static_cast<size_t>(bytes_read - internal_offset), n);

        if (target_buf != scratch) {
            std::memcpy(scratch, target_buf + internal_offset,
                        actual_payload_bytes);
        }
        *result = Slice(scratch, actual_payload_bytes);
    } else {
        *result = Slice(scratch, bytes_read);
    }

    return Status::ok();
}

// ---------------------------------------------------------------------------
// WinWritableFileWriter
// ---------------------------------------------------------------------------

Status WinWritableFileWriter::submit_write_(const char *src, size_t len,
                                            uint64_t offset) {
    size_t total_written = 0;

    while (total_written < len) {
        OVERLAPPED overlapped = {};
        overlapped.Offset = static_cast<DWORD>(offset + total_written);
        overlapped.OffsetHigh =
            static_cast<DWORD>((offset + total_written) >> 32);

        const char *current_src = src + total_written;
        size_t current_len = len - total_written;

        DWORD chunk = (current_len > MAXDWORD)
                          ? MAXDWORD
                          : static_cast<DWORD>(current_len);

        DWORD bytes_transferred = 0;
        BOOL ok = ::WriteFile(hFile_, current_src, chunk, &bytes_transferred,
                              &overlapped);

        if (!ok) {
            DWORD err = ::GetLastError();
            if (err != ERROR_IO_PENDING) {
                return win_error(filename_, err);
            }
            if (!::GetOverlappedResult(hFile_, &overlapped, &bytes_transferred,
                                       TRUE)) {
                return win_error(filename_, ::GetLastError());
            }
        }

        if (bytes_transferred == 0) {
            return win_error(filename_, ERROR_DISK_FULL);
        }

        total_written += static_cast<size_t>(bytes_transferred);

        if (direct_io_ && (bytes_transferred % k_o_direct_alignment != 0) &&
            (total_written < len)) {
            return win_error(filename_, ERROR_INVALID_PARAMETER);
        }
    }

    return Status::ok();
}

Status WinWritableFileWriter::append(const Slice &data) {
    const char *src = data.data();
    size_t left = data.size();

    while (left > 0) {
        if (direct_io_ && buf_.current_size() == 0) {
            bool is_src_aligned =
                (reinterpret_cast<uintptr_t>(src) % k_o_direct_alignment == 0);
            bool is_offset_aligned = (filesize_ % k_o_direct_alignment == 0);
            bool is_len_aligned = (left % k_o_direct_alignment == 0);

            if (is_src_aligned && is_offset_aligned && is_len_aligned) {
                Status s = submit_write_(src, left, filesize_);
                if (!s.is_ok())
                    return s;
                filesize_ += left;
                return Status::ok();
            }
        }

        size_t written = buf_.append(src, left);
        src += written;
        left -= written;
        filesize_ += written;

        if (buf_.current_size() == buf_.capacity()) {
            Status s = flush_buffer();
            if (!s.is_ok())
                return s;
        }
    }

    return Status::ok();
}

Status WinWritableFileWriter::close() {
    Status s = flush_buffer();

    if (direct_io_ && s.is_ok()) {
        // Truncate to the logical file size (padded writes may have extended
        // it).
        LARGE_INTEGER pos;
        pos.QuadPart = static_cast<LONGLONG>(filesize_);
        if (!::SetFilePointerEx(hFile_, pos, nullptr, FILE_BEGIN)) {
            s = win_error(filename_, ::GetLastError());
        }
        if (s.is_ok() && !::SetEndOfFile(hFile_)) {
            s = win_error(filename_, ::GetLastError());
        }
    }

    if (hFile_ != INVALID_HANDLE_VALUE) {
        if (!::CloseHandle(hFile_) && s.is_ok()) {
            s = win_error(filename_, ::GetLastError());
        }
        hFile_ = INVALID_HANDLE_VALUE;
    }

    return s;
}

Status WinWritableFileWriter::sync() {
    Status s = flush_buffer();
    if (!s.is_ok())
        return s;

    if (!::FlushFileBuffers(hFile_)) {
        return win_error(filename_, ::GetLastError());
    }

    // On Windows, flushing the file buffers is sufficient for durability.
    // For MANIFEST files we also sync the parent directory.
    s = sync_dir_if_manifest();
    if (!s.is_ok())
        return s;

    return Status::ok();
}

Status WinWritableFileWriter::flush_buffer() {
    if (buf_.current_size() == 0)
        return Status::ok();

    size_t bytes_to_write = buf_.current_size();
    uint64_t write_offset = filesize_ - buf_.current_size();

    if (direct_io_) {
        size_t remainder = buf_.current_size() % k_o_direct_alignment;
        if (remainder != 0) {
            size_t padded_size =
                buf_.current_size() + (k_o_direct_alignment - remainder);
            std::memset(buf_.data() + buf_.current_size(), 0,
                        k_o_direct_alignment - remainder);
            bytes_to_write = padded_size;
        }
    }

    Status s = submit_write_(buf_.data(), bytes_to_write, write_offset);
    if (!s.is_ok())
        return s;

    buf_.clear();
    return Status::ok();
}

Status WinWritableFileWriter::sync_dir_if_manifest() {
    Status status;
    if (!is_manifest_) {
        return status;
    }

    // Open the parent directory and flush its buffers.
    HANDLE hDir = ::CreateFileA(
        dirname_.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (hDir == INVALID_HANDLE_VALUE) {
        status = win_error(dirname_, ::GetLastError());
    } else {
        status = sync_handle(hDir, dirname_);
        ::CloseHandle(hDir);
    }
    return status;
}

Status WinWritableFileWriter::sync_handle(HANDLE hFile,
                                          const std::string &file_path) {
    if (!::FlushFileBuffers(hFile)) {
        return win_error(file_path, ::GetLastError());
    }
    return Status::ok();
}

std::string WinWritableFileWriter::dirname(const std::string &filename) {
    // Handle both forward-slash and backslash separators.
    std::string::size_type separator_pos = filename.rfind('/');
    std::string::size_type backslash_pos = filename.rfind('\\');
    if (backslash_pos != std::string::npos &&
        (separator_pos == std::string::npos || backslash_pos > separator_pos)) {
        separator_pos = backslash_pos;
    }
    if (separator_pos == std::string::npos) {
        return std::string(".");
    }
    return filename.substr(0, separator_pos);
}

Slice WinWritableFileWriter::basename(const std::string &filename) {
    std::string::size_type separator_pos = filename.rfind('/');
    std::string::size_type backslash_pos = filename.rfind('\\');
    if (backslash_pos != std::string::npos &&
        (separator_pos == std::string::npos || backslash_pos > separator_pos)) {
        separator_pos = backslash_pos;
    }
    if (separator_pos == std::string::npos) {
        return Slice(filename);
    }
    return Slice(filename.data() + separator_pos + 1,
                 filename.length() - separator_pos - 1);
}

int lock_or_unlock(HANDLE hFile, bool lock) {
    OVERLAPPED overlapped = {};
    DWORD flags = LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY;
    if (!lock) {
        flags = 0; // Unlock.
    }

    // Lock/unlock the entire file.
    BOOL ok;
    if (lock) {
        ok = ::LockFileEx(hFile, flags, 0, MAXDWORD, MAXDWORD, &overlapped);
    } else {
        ok = ::UnlockFileEx(hFile, 0, MAXDWORD, MAXDWORD, &overlapped);
    }

    if (!ok) {
        return -1;
    }
    return 0;
}

} // namespace anchordb
