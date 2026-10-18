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

#include "sstable/table.h"
#include "anchordb/status.h"
#include "sstable/data_record.h"
#include "sstable/empty_iter.h"
#include "sstable/idx_block_reader.h"
#include "sstable/idx_block_writer.h"
#include "sstable/idx_summary.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "anchordb/anchordb_namespace.h"

namespace ANCHORDB_NAMESPACE {

std::string table_file_name(std::string_view dir, uint64_t table_number,
                            TableFilePart part) {
    const char *suffix = "";
    switch (part) {
    case TableFilePart::data: suffix = ".data"; break;
    case TableFilePart::index: suffix = ".index"; break;
    case TableFilePart::summary: suffix = ".summary"; break;
    }

    char number[32];
    std::snprintf(number, sizeof(number), "%06llu",
                  static_cast<unsigned long long>(table_number));

    std::string result;
    result.reserve(dir.size() + sizeof(number) + 8);
    if (!dir.empty()) {
        result.append(dir.data(), dir.size());
        result.push_back('/');
    }
    result.append(number);
    result.append(suffix);
    return result;
}

TableFileSet make_table_file_set(std::string_view dir, uint64_t table_number) {
    TableFileSet set;
    set.data = table_file_name(dir, table_number, TableFilePart::data);
    set.index = table_file_name(dir, table_number, TableFilePart::index);
    set.summary = table_file_name(dir, table_number, TableFilePart::summary);
    return set;
}

Status Table::read_exact(RandomAccessFileReader *reader, uint64_t offset,
                         size_t n, std::string *buf, std::string_view *out) {
    *out = std::string_view();
    if (n == 0) {
        buf->clear();
        return Status::ok();
    }

    buf->resize(n);
    std::string_view view;
    Status s = reader->read(offset, n, &view, buf->data());
    if (!s.is_ok()) { return s; }
    if (view.size() < n) {
        return Status::corruption("short read", std::to_string(view.size()));
    }
    if (view.data() != buf->data()) {
        std::memmove(buf->data(), view.data(), n);
    }
    *out = std::string_view(buf->data(), n);
    return Status::ok();
}

Status Table::open(const Options &options, const TableFileSet &files,
                   std::shared_ptr<Table> *result) {
    if (result == nullptr) {
        return Status::invalid_argument("Table::open: null result");
    }
    if (options.env == nullptr) {
        return Status::invalid_argument("Table::open: options.env is null");
    }

    uint64_t summary_size = 0;
    uint64_t index_size = 0;
    uint64_t data_size = 0;
    Status s = options.env->get_file_size(files.summary, &summary_size);
    if (!s.is_ok()) { return s; }
    s = options.env->get_file_size(files.index, &index_size);
    if (!s.is_ok()) { return s; }
    s = options.env->get_file_size(files.data, &data_size);
    if (!s.is_ok()) { return s; }
    if (summary_size > kMaxSummaryFileSize) {
        return Status::corruption("summary file too large",
                                  std::to_string(summary_size));
    }
    if (index_size % kDefaultBlockSize != 0) {
        return Status::corruption("index file not block aligned",
                                  std::to_string(index_size));
    }

    auto table = std::shared_ptr<Table>(new Table());
    table->comparator_ = options.comparator;
    if (table->comparator_.compare == nullptr) {
        table->comparator_ = new_byte_wise_comparator();
    }
    table->summary_buffer_.clear();
    table->index_file_size_ = index_size;
    table->data_file_size_ = data_size;
    table->block_count_ = static_cast<size_t>(index_size / kDefaultBlockSize);

    RandomAccessFileReader *raw_index = nullptr;
    s = options.env->new_random_access_file_reader(files.index, &raw_index);
    if (!s.is_ok()) { return s; }
    table->index_reader_.reset(raw_index);

    RandomAccessFileReader *raw_data = nullptr;
    s = options.env->new_random_access_file_reader(files.data, &raw_data);
    if (!s.is_ok()) { return s; }
    table->data_reader_.reset(raw_data);

    RandomAccessFileReader *raw_summary = nullptr;
    s = options.env->new_random_access_file_reader(files.summary, &raw_summary);
    if (!s.is_ok()) { return s; }
    std::unique_ptr<RandomAccessFileReader> summary_reader(raw_summary);

    std::string_view summary_view;
    s = read_exact(summary_reader.get(), 0, static_cast<size_t>(summary_size),
                   &table->summary_buffer_, &summary_view);
    if (!s.is_ok()) { return s; }
    // summary_reader is closed here; the summary bytes are fully resident.
    summary_reader.reset();

    table->summary_ = std::make_unique<SummaryIndexReader>(
        table->summary_buffer_, table->comparator_);
    if (!table->summary_->valid()) {
        return Status::corruption("invalid summary index");
    }
    if (table->summary_->size() != table->block_count_) {
        return Status::corruption("summary/index block count mismatch");
    }

    *result = std::move(table);
    return Status::ok();
}

Status Table::read_index_block(uint64_t block_offset, std::string *buf,
                               std::string_view *out) const {
    if (block_offset + kDefaultBlockSize > index_file_size_) {
        return Status::corruption("index block offset out of range",
                                  std::to_string(block_offset));
    }
    return read_exact(index_reader_.get(), block_offset, kDefaultBlockSize, buf,
                      out);
}

Status Table::read_record(const ValueHandle &handle,
                          std::string_view expected_key, std::string *value,
                          std::string *scratch) const {
    if (value == nullptr || scratch == nullptr) {
        return Status::invalid_argument("Table::read_record: null argument");
    }
    if (handle.offset >= data_file_size_) {
        return Status::corruption("data record offset out of range",
                                  std::to_string(handle.offset));
    }

    const uint64_t remaining = data_file_size_ - handle.offset;
    if (remaining < kDataRecordHeaderSize) {
        return Status::corruption("truncated data record header");
    }

    size_t probe =
        static_cast<size_t>(std::min<uint64_t>(remaining, kDefaultBlockSize));
    while (true) {
        const size_t n =
            static_cast<size_t>(std::min<uint64_t>(probe, remaining));
        std::string_view view;
        Status s =
            read_exact(data_reader_.get(), handle.offset, n, scratch, &view);
        if (!s.is_ok()) { return s; }

        size_t total = 0;
        bool complete = false;
        s = data_record_size(view, &total, &complete);
        if (!s.is_ok()) { return s; }
        if (!complete || total > view.size()) {
            if (n >= remaining) {
                return Status::corruption("truncated data record");
            }
            if (total <= n) {
                return Status::corruption("malformed data record length");
            }
            if (total > remaining) {
                return Status::corruption(
                    "data record extends past end of file");
            }
            probe = total;
            continue;
        }

        ParsedDataRecord rec;
        std::string_view cursor = view;
        s = decode_data_record(&cursor, &rec);
        if (!s.is_ok()) { return s; }
        if (rec.key != expected_key) {
            return Status::corruption("data record key mismatch");
        }
        if (rec.type != handle.type || rec.flag != handle.flag) {
            return Status::corruption("index/data record metadata mismatch");
        }
        if (rec.type == ValueType::deletion) {
            return Status::not_found("tombstone", std::string(expected_key));
        }
        if (rec.flag == ValueFlag::blob) {
            return Status::not_supported("vLog read not implemented");
        }
        value->assign(rec.value.data(), rec.value.size());
        return Status::ok();
    }
}

Status Table::get(const ReadOptions &options, std::string_view key,
                  std::string *value, std::string *scratch) const {
    (void)options;
    if (value == nullptr) {
        return Status::invalid_argument("Table::get: null value");
    }
    if (scratch == nullptr) {
        return Status::invalid_argument("Table::get: null scratch");
    }

    uint64_t block_offset = 0;
    if (!summary_->seek(key, &block_offset)) {
        return Status::not_found("key not in table range", std::string(key));
    }

    std::string block_buf;
    std::string_view block;
    Status s = read_index_block(block_offset, &block_buf, &block);
    if (!s.is_ok()) { return s; }

    IndexBlockReader reader(block);
    if (!reader.valid()) { return Status::corruption("invalid index block"); }

    size_t lo = 0;
    size_t hi = reader.size();
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (comparator_.compare(reader.key(mid), key) < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo >= reader.size() || comparator_.compare(reader.key(lo), key) != 0) {
        return Status::not_found("key not found", std::string(key));
    }

    return read_record(reader.entry(lo).handle, key, value, scratch);
}

Iterator *Table::new_iterator(const ReadOptions &options) const {
    if (!summary_ || !summary_->valid()) {
        return new_error_iterator(Status::corruption("invalid table"));
    }
    if (block_count_ == 0) { return new_empty_iterator(); }
    return new TableIterator(shared_from_this(), options);
}

TableIterator::TableIterator(std::shared_ptr<const Table> table,
                             ReadOptions options)
    : table_(std::move(table)), options_(options),
      comparator_(table_->comparator()), block_count_(table_->block_count()) {}

TableIterator::~TableIterator() = default;

bool TableIterator::valid() const { return valid_; }

void TableIterator::invalidate() {
    valid_ = false;
    reader_.reset();
    key_ = std::string_view();
    value_ = std::string_view();
}

bool TableIterator::visible(size_t slot) const {
    return reader_->entry(slot).handle.type != ValueType::deletion;
}

void TableIterator::load_block(size_t block_index) {
    block_index_ = block_index;
    reader_.reset();
    std::string_view view;
    Status s = table_->read_index_block(static_cast<uint64_t>(block_index) *
                                            kDefaultBlockSize,
                                        &block_buf_, &view);
    if (!s.is_ok()) {
        status_ = s;
        invalidate();
        return;
    }
    reader_ = std::make_unique<IndexBlockReader>(view);
    if (!reader_->valid()) {
        status_ = Status::corruption("invalid index block");
        invalidate();
    }
}

void TableIterator::materialize_current() {
    const IndexBlockEntry entry = reader_->entry(slot_);
    key_ = entry.key;
    value_buf_.clear();
    Status s = table_->read_record(entry.handle, key_, &value_buf_, &scratch_);
    if (!s.is_ok()) {
        status_ = s;
        invalidate();
        return;
    }
    value_ = value_buf_;
    valid_ = true;
}

void TableIterator::skip_forward() {
    while (true) {
        if (reader_ == nullptr) {
            invalidate();
            return;
        }
        if (slot_ < reader_->size()) {
            if (visible(slot_)) {
                materialize_current();
                return;
            }
            ++slot_;
            continue;
        }
        if (block_index_ + 1 >= block_count_) {
            invalidate();
            return;
        }
        load_block(block_index_ + 1);
        if (!status_.is_ok()) {
            invalidate();
            return;
        }
        slot_ = 0;
    }
}

void TableIterator::skip_backward() {
    while (true) {
        if (reader_ == nullptr) {
            invalidate();
            return;
        }
        if (slot_ >= reader_->size()) {
            // Positioned just past the end of a block: step to its last entry,
            // or move to the previous block if it is empty.
            if (reader_->size() == 0) {
                if (block_index_ == 0) {
                    invalidate();
                    return;
                }
                load_block(block_index_ - 1);
                if (!status_.is_ok()) {
                    invalidate();
                    return;
                }
                continue;
            }
            slot_ = reader_->size() - 1;
            continue;
        }
        if (visible(slot_)) {
            materialize_current();
            return;
        }
        if (slot_ == 0) {
            if (block_index_ == 0) {
                invalidate();
                return;
            }
            load_block(block_index_ - 1);
            if (!status_.is_ok()) {
                invalidate();
                return;
            }
            slot_ = reader_->size();
            continue;
        }
        --slot_;
    }
}

void TableIterator::seek_to_first() {
    status_ = Status::ok();
    valid_ = false;
    if (block_count_ == 0) {
        invalidate();
        return;
    }
    load_block(0);
    if (!status_.is_ok()) { return; }
    slot_ = 0;
    skip_forward();
}

void TableIterator::seek_to_last() {
    status_ = Status::ok();
    valid_ = false;
    if (block_count_ == 0) {
        invalidate();
        return;
    }
    load_block(block_count_ - 1);
    if (!status_.is_ok()) { return; }
    slot_ = reader_->size();
    skip_backward();
}

void TableIterator::seek(std::string_view target) {
    status_ = Status::ok();
    valid_ = false;
    if (block_count_ == 0) {
        invalidate();
        return;
    }

    uint64_t block_offset = 0;
    if (!table_->summary_->seek(target, &block_offset)) {
        invalidate();
        return;
    }
    const size_t block_index =
        static_cast<size_t>(block_offset / kDefaultBlockSize);
    load_block(block_index);
    if (!status_.is_ok()) { return; }

    size_t lo = 0;
    size_t hi = reader_->size();
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (comparator_.compare(reader_->key(mid), target) < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    slot_ = lo;
    skip_forward();
}

void TableIterator::next() {
    assert(valid_);
    ++slot_;
    skip_forward();
}

void TableIterator::prev() {
    assert(valid_);
    if (slot_ > 0) {
        --slot_;
        skip_backward();
        return;
    }
    if (block_index_ == 0) {
        invalidate();
        return;
    }
    load_block(block_index_ - 1);
    if (!status_.is_ok()) {
        invalidate();
        return;
    }
    slot_ = reader_->size();
    skip_backward();
}

std::string_view TableIterator::key() const {
    assert(valid_);
    return key_;
}

std::string_view TableIterator::value() const {
    assert(valid_);
    return value_;
}

Status TableIterator::status() const { return status_; }

} // namespace ANCHORDB_NAMESPACE